// Shift Happens - Game Off 2026

#include "ShiftGlass.h"

#include "Containers/Ticker.h"
#include "Containers/UnrealString.h"
#include "CoreGlobals.h"
#include "GenericPlatform/GenericPlatformCompilerPreSetup.h"
#include "HAL/IConsoleManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Logging/LogCategory.h"
#include "Logging/LogMacros.h"
#include "Math/Color.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreMisc.h"
#include "Misc/CString.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "RHIDefinitions.h"

#if PLATFORM_WINDOWS

#include "Containers/Array.h"
#include "Containers/ContainersFwd.h"
#include "Features/IModularFeatures.h"
#include "GenericPlatform/ICursor.h"
#include "HAL/Platform.h"
#include "Templates/RefCounting.h"
#include "Windows/IDXGISwapchainProvider.h"
#include "Windows/PreWindowsApi.h"
#include "Windows/AllowWindowsPlatformTypes.h"
#include "Unknwnbase.h"
#include "Uxtheme.h"
#include "Windows.h"
#include "combaseapi.h"
#include "dxgi.h"
#include "dxgi1_2.h"
#include "dxgi1_3.h"
#include "dxgi1_4.h"
#include "dxgi1_5.h"
#include "dxgicommon.h"
#include "dxgiformat.h"
#include "dxgitype.h"
#include "guiddef.h"
#include "handleapi.h"
#include "synchapi.h"
THIRD_PARTY_INCLUDES_START
#include <d3d12.h>
#include <dcomp.h>
#include <dwmapi.h>
THIRD_PARTY_INCLUDES_END
#include <atomic>

#endif   // PLATFORM_WINDOWS

DEFINE_LOG_CATEGORY_STATIC(LogShiftGlass, All, All);

void ShiftGlass::SavePictures(const TArray<FColor>& Pixels, int32 Width, int32 Height, const FString& Prefix)
{
	if (Width <= 0 || Height <= 0 || Pixels.Num() < Width * Height)
	{
		return;
	}
	int64 AlphaSum = 0;
	for (const FColor& Pixel : Pixels)
	{
		AlphaSum += Pixel.A;
	}
	UE_LOG(LogShiftGlass, Display, TEXT("Glass: pictures %dx%d, mean alpha %.1f -> %s_*.png"), Width, Height, static_cast<double>(AlphaSum) / FMath::Max(1, Width * Height), *Prefix);
	const auto Save = [&](const TArray<FColor>& Out, const TCHAR* Name)
		{
			TArray64<uint8> Png;
			const FImageView View(const_cast<FColor*>(Out.GetData()), Width, Height, ERawImageFormat::BGRA8);
			FImageUtils::CompressImage(Png, TEXT("png"), View);
			FFileHelper::SaveArrayToFile(Png, *FString::Printf(TEXT("%s_%s.png"), *Prefix, Name));
		};
	{
		// the colour exactly as the compositor is given it (premultiplied by the alpha), and the alpha as grey
		TArray<FColor> Colour, Alpha;
		Colour.SetNumUninitialized(Pixels.Num());
		Alpha.SetNumUninitialized(Pixels.Num());
		for (int32 i = 0; i < Pixels.Num(); ++i)
		{
			Colour[i] = FColor(Pixels[i].R, Pixels[i].G, Pixels[i].B, 255);
			Alpha[i] = FColor(Pixels[i].A, Pixels[i].A, Pixels[i].A, 255);
		}
		Save(Colour, TEXT("rgb"));
		Save(Alpha, TEXT("alpha"));
	}
	static const FColor Bars[6] = { FColor(229, 57, 53), FColor(67, 160, 71), FColor(30, 136, 229), FColor(253, 216, 53), FColor(142, 36, 170), FColor(0, 172, 193) };
	for (int32 Kind = 0; Kind < 3; ++Kind)
	{
		// what the desktop shows: the premultiplied colour plus what is left of the desktop behind it, over a striped, a black and a white desktop
		TArray<FColor> Out;
		Out.SetNumUninitialized(Pixels.Num());
		for (int32 y = 0; y < Height; ++y)
		{
			for (int32 x = 0; x < Width; ++x)
			{
				const FColor S = Pixels[y * Width + x];
				const FColor B = Kind == 0 ? Bars[FMath::Min(5, x * 6 / Width)] : (Kind == 1 ? FColor(0, 0, 0) : FColor(255, 255, 255));
				const int32 Inv = 255 - S.A;
				Out[y * Width + x] = FColor(static_cast<uint8>(FMath::Min(255, S.R + B.R * Inv / 255)), static_cast<uint8>(FMath::Min(255, S.G + B.G * Inv / 255)),
					static_cast<uint8>(FMath::Min(255, S.B + B.B * Inv / 255)), 255);
			}
		}
		Save(Out, Kind == 0 ? TEXT("stripes") : (Kind == 1 ? TEXT("black") : TEXT("white")));
	}
}

#if PLATFORM_WINDOWS

namespace
{
	std::atomic<bool> GGlassActive{ false };

	constexpr UINT GlassFlagAllowModeSwitch = 2;     // DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH
	constexpr UINT GlassFlagAllowTearing = 2048;     // DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
	constexpr UINT GlassPresentAllowTearing = 0x200; // DXGI_PRESENT_ALLOW_TEARING

	/**
	 * QA, -ShiftGlassDump=<prefix>: the back buffer exactly as the desktop compositor gets it (colour premultiplied, alpha = opacity), copied out just before a present (a
	 * screenshot would force the alpha to 255), laid over a striped, a black and a white desktop in software and saved as <prefix>_stripes / _black / _white.png.
	 * Needs no visible screen.
	 */
	void DumpBackBuffer(ID3D12CommandQueue* Queue, IDXGISwapChain4* SwapChain, const FString& Prefix)
	{
		TRefCountPtr<ID3D12Device> Device;
		TRefCountPtr<ID3D12Resource> Buffer;
		if (FAILED(Queue->GetDevice(IID_PPV_ARGS(Device.GetInitReference()))) || FAILED(SwapChain->GetBuffer(SwapChain->GetCurrentBackBufferIndex(), IID_PPV_ARGS(Buffer.GetInitReference()))))
		{
			return;
		}
		const D3D12_RESOURCE_DESC Desc = Buffer->GetDesc();
		D3D12_PLACED_SUBRESOURCE_FOOTPRINT Footprint = {};
		UINT64 TotalBytes = 0;
		Device->GetCopyableFootprints(&Desc, 0, 1, 0, &Footprint, nullptr, nullptr, &TotalBytes);
		D3D12_HEAP_PROPERTIES Heap = {};
		Heap.Type = D3D12_HEAP_TYPE_READBACK;
		D3D12_RESOURCE_DESC ReadDesc = {};
		ReadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		ReadDesc.Width = TotalBytes;
		ReadDesc.Height = 1;
		ReadDesc.DepthOrArraySize = 1;
		ReadDesc.MipLevels = 1;
		ReadDesc.SampleDesc.Count = 1;
		ReadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		TRefCountPtr<ID3D12Resource> Readback;
		TRefCountPtr<ID3D12CommandAllocator> Allocator;
		TRefCountPtr<ID3D12GraphicsCommandList> List;
		TRefCountPtr<ID3D12Fence> Fence;
		if (FAILED(Device->CreateCommittedResource(&Heap, D3D12_HEAP_FLAG_NONE, &ReadDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(Readback.GetInitReference())))
			|| FAILED(Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(Allocator.GetInitReference())))
			|| FAILED(Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, Allocator.GetReference(), nullptr, IID_PPV_ARGS(List.GetInitReference())))
			|| FAILED(Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(Fence.GetInitReference()))))
		{
			UE_LOG(LogShiftGlass, Warning, TEXT("Glass: the back buffer dump could not set up its copy"));
			return;
		}
		D3D12_TEXTURE_COPY_LOCATION Dst = {};
		Dst.pResource = Readback.GetReference();
		Dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		Dst.PlacedFootprint = Footprint;
		D3D12_TEXTURE_COPY_LOCATION Src = {};
		Src.pResource = Buffer.GetReference();
		Src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		Src.SubresourceIndex = 0;
		List->CopyTextureRegion(&Dst, 0, 0, 0, &Src, nullptr);
		List->Close();
		ID3D12CommandList* Lists[] = { List.GetReference() };
		Queue->ExecuteCommandLists(1, Lists);
		Queue->Signal(Fence.GetReference(), 1);
		HANDLE Event = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
		Fence->SetEventOnCompletion(1, Event);
		::WaitForSingleObject(Event, 5000);
		::CloseHandle(Event);

		const int32 Width = static_cast<int32>(Desc.Width);
		const int32 Height = static_cast<int32>(Desc.Height);
		void* Mapped = nullptr;
		if (FAILED(Readback->Map(0, nullptr, &Mapped)) || !Mapped)
		{
			return;
		}
		const bool bBgra = Desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM || Desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
		TArray<FColor> Pixels;
		Pixels.SetNumUninitialized(Width * Height);
		for (int32 y = 0; y < Height; ++y)
		{
			const uint8* Row = static_cast<const uint8*>(Mapped) + static_cast<size_t>(y) * Footprint.Footprint.RowPitch;
			for (int32 x = 0; x < Width; ++x)
			{
				const uint8* P = Row + x * 4;
				Pixels[y * Width + x] = bBgra ? FColor(P[2], P[1], P[0], P[3]) : FColor(P[0], P[1], P[2], P[3]);
			}
		}
		Readback->Unmap(0, nullptr);
		UE_LOG(LogShiftGlass, Display, TEXT("Glass: back buffer dump %dx%d format %d"), Width, Height, static_cast<int32>(Desc.Format));
		ShiftGlass::SavePictures(Pixels, Width, Height, Prefix);
	}

	/**
	 * The swap chain the engine gets: a composition swap chain behind the interface of a window's swap chain. The D3D12 RHI always asks for the flags of a window swap chain
	 * (mode switch, tearing) when it resizes and presents, and may ask for exclusive fullscreen; a composition swap chain has neither. This forwards everything and only
	 * strips those flags and answers the fullscreen calls as a window that never is fullscreen (the game maps its Fullscreen setting to a borderless window).
	 */
	class FShiftGlassSwapChain final : public IDXGISwapChain4
	{
	public:
		explicit FShiftGlassSwapChain(IDXGISwapChain4* InInner, ID3D12CommandQueue* InQueue)
			: Inner(InInner)
			, Queue(InQueue)
		{
			FParse::Value(FCommandLine::Get(), TEXT("ShiftGlassDump="), DumpPrefix);
		}

		// IUnknown
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID Riid, void** Object) override
		{
			if (!Object)
			{
				return E_POINTER;
			}
			if (Riid == __uuidof(IUnknown) || Riid == __uuidof(IDXGIObject) || Riid == __uuidof(IDXGIDeviceSubObject) || Riid == __uuidof(IDXGISwapChain) || Riid == __uuidof(IDXGISwapChain1)
				|| Riid == __uuidof(IDXGISwapChain2) || Riid == __uuidof(IDXGISwapChain3) || Riid == __uuidof(IDXGISwapChain4))
			{
				*Object = static_cast<IDXGISwapChain4*>(this);
				AddRef();
				return S_OK;
			}
			return Inner->QueryInterface(Riid, Object);
		}
		ULONG STDMETHODCALLTYPE AddRef() override
		{
			return ++RefCount;
		}
		ULONG STDMETHODCALLTYPE Release() override
		{
			const ULONG Count = --RefCount;
			if (Count == 0)
			{
				delete this;
			}
			return Count;
		}

		// IDXGIObject
		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID Name, UINT DataSize, const void* Data) override { return Inner->SetPrivateData(Name, DataSize, Data); }
		HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID Name, const IUnknown* Unknown) override { return Inner->SetPrivateDataInterface(Name, Unknown); }
		HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID Name, UINT* DataSize, void* Data) override { return Inner->GetPrivateData(Name, DataSize, Data); }
		HRESULT STDMETHODCALLTYPE GetParent(REFIID Riid, void** Parent) override { return Inner->GetParent(Riid, Parent); }

		// IDXGIDeviceSubObject
		HRESULT STDMETHODCALLTYPE GetDevice(REFIID Riid, void** Device) override { return Inner->GetDevice(Riid, Device); }

		// IDXGISwapChain
		HRESULT STDMETHODCALLTYPE Present(UINT SyncInterval, UINT Flags) override
		{
			const HRESULT Result = Inner->Present(FMath::Max(1u, SyncInterval), Flags & ~GlassPresentAllowTearing);
			NoteFrame(TEXT("Present"), Result);
			return Result;
		}
		HRESULT STDMETHODCALLTYPE GetBuffer(UINT Buffer, REFIID Riid, void** Surface) override { return Inner->GetBuffer(Buffer, Riid, Surface); }
		HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL Fullscreen, IDXGIOutput* Target) override { return S_OK; }
		HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* Fullscreen, IDXGIOutput** Target) override
		{
			if (Fullscreen)
			{
				*Fullscreen = FALSE;
			}
			if (Target)
			{
				*Target = nullptr;
			}
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* Desc) override { return Inner->GetDesc(Desc); }
		HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format, UINT Flags) override
		{
			const HRESULT Result = Inner->ResizeBuffers(BufferCount, Width, Height, Format, Flags & ~(GlassFlagAllowModeSwitch | GlassFlagAllowTearing));
			if (FAILED(Result)) UE_LOG(LogShiftGlass, Warning, TEXT("Glass: ResizeBuffers(%u, %ux%u, format %d, flags %u) -> 0x%08x"), BufferCount, Width, Height, static_cast<int32>(Format), Flags, static_cast<uint32>(Result));
			return Result;
		}
		HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* NewTarget) override { return S_OK; }
		HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** Output) override { return Inner->GetContainingOutput(Output); }
		HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* Stats) override { return Inner->GetFrameStatistics(Stats); }
		HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* Count) override { return Inner->GetLastPresentCount(Count); }

		// IDXGISwapChain1
		HRESULT STDMETHODCALLTYPE GetDesc1(DXGI_SWAP_CHAIN_DESC1* Desc) override { return Inner->GetDesc1(Desc); }
		HRESULT STDMETHODCALLTYPE GetFullscreenDesc(DXGI_SWAP_CHAIN_FULLSCREEN_DESC* Desc) override { return Inner->GetFullscreenDesc(Desc); }
		HRESULT STDMETHODCALLTYPE GetHwnd(HWND* Hwnd) override { return Inner->GetHwnd(Hwnd); }
		HRESULT STDMETHODCALLTYPE GetCoreWindow(REFIID Riid, void** Unknown) override { return Inner->GetCoreWindow(Riid, Unknown); }
		HRESULT STDMETHODCALLTYPE Present1(UINT SyncInterval, UINT Flags, const DXGI_PRESENT_PARAMETERS* Parameters) override
		{
			const HRESULT Result = Inner->Present1(FMath::Max(1u, SyncInterval), Flags & ~GlassPresentAllowTearing, Parameters);
			NoteFrame(TEXT("Present1"), Result);
			return Result;
		}
		BOOL STDMETHODCALLTYPE IsTemporaryMonoSupported() override { return Inner->IsTemporaryMonoSupported(); }
		HRESULT STDMETHODCALLTYPE GetRestrictToOutput(IDXGIOutput** Output) override { return Inner->GetRestrictToOutput(Output); }
		HRESULT STDMETHODCALLTYPE SetBackgroundColor(const DXGI_RGBA* Color) override { return Inner->SetBackgroundColor(Color); }
		HRESULT STDMETHODCALLTYPE GetBackgroundColor(DXGI_RGBA* Color) override { return Inner->GetBackgroundColor(Color); }
		HRESULT STDMETHODCALLTYPE SetRotation(DXGI_MODE_ROTATION Rotation) override { return Inner->SetRotation(Rotation); }
		HRESULT STDMETHODCALLTYPE GetRotation(DXGI_MODE_ROTATION* Rotation) override { return Inner->GetRotation(Rotation); }

		// IDXGISwapChain2
		HRESULT STDMETHODCALLTYPE SetSourceSize(UINT Width, UINT Height) override { return Inner->SetSourceSize(Width, Height); }
		HRESULT STDMETHODCALLTYPE GetSourceSize(UINT* Width, UINT* Height) override { return Inner->GetSourceSize(Width, Height); }
		HRESULT STDMETHODCALLTYPE SetMaximumFrameLatency(UINT MaxLatency) override { return Inner->SetMaximumFrameLatency(MaxLatency); }
		HRESULT STDMETHODCALLTYPE GetMaximumFrameLatency(UINT* MaxLatency) override { return Inner->GetMaximumFrameLatency(MaxLatency); }
		HANDLE STDMETHODCALLTYPE GetFrameLatencyWaitableObject() override { return Inner->GetFrameLatencyWaitableObject(); }
		HRESULT STDMETHODCALLTYPE SetMatrixTransform(const DXGI_MATRIX_3X2_F* Matrix) override { return Inner->SetMatrixTransform(Matrix); }
		HRESULT STDMETHODCALLTYPE GetMatrixTransform(DXGI_MATRIX_3X2_F* Matrix) override { return Inner->GetMatrixTransform(Matrix); }

		// IDXGISwapChain3
		UINT STDMETHODCALLTYPE GetCurrentBackBufferIndex() override { return Inner->GetCurrentBackBufferIndex(); }
		HRESULT STDMETHODCALLTYPE CheckColorSpaceSupport(DXGI_COLOR_SPACE_TYPE ColorSpace, UINT* Support) override { return Inner->CheckColorSpaceSupport(ColorSpace, Support); }
		HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace) override { return Inner->SetColorSpace1(ColorSpace); }
		HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format, UINT Flags, const UINT* NodeMask, IUnknown* const* PresentQueue) override
		{
			return Inner->ResizeBuffers1(BufferCount, Width, Height, Format, Flags & ~(GlassFlagAllowModeSwitch | GlassFlagAllowTearing), NodeMask, PresentQueue);
		}

		// IDXGISwapChain4
		HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE Type, UINT Size, void* MetaData) override { return Inner->SetHDRMetaData(Type, Size, MetaData); }

	private:
		/** Logs the first present and the first failures, so a window that shows nothing can be told from a swap chain that is not being fed. */
		void NoteFrame(const TCHAR* Call, HRESULT Result)
		{
			const uint64 Count = ++Presents;
			if (Count == 900 && !DumpPrefix.IsEmpty() && Queue.IsValid())
			{
				DumpBackBuffer(Queue.GetReference(), Inner.GetReference(), DumpPrefix);
			}
			if (Count == 1 || (FAILED(Result) && Failures++ < 5))
			{
				UE_LOG(LogShiftGlass, Log, TEXT("Glass: %s #%llu -> 0x%08x"), Call, Count, static_cast<uint32>(Result));
			}
		}

		TRefCountPtr<IDXGISwapChain4> Inner;
		TRefCountPtr<ID3D12CommandQueue> Queue;
		FString DumpPrefix;
		std::atomic<uint64> Presents{ 0 };
		std::atomic<uint32> Failures{ 0 };
		std::atomic<ULONG> RefCount{ 1 };
	};

	WNDPROC GOldWindowProc = nullptr;
	void UpdateGlassOffset(HWND Window);   // defined with the composition objects below

	/** Paints the window's own surface black: with the frame extended over the client area, black is glass (transparent), and the surface is what shows wherever the swap chain is clear. */
	void PaintSurfaceBlack(HWND Window, HDC Dc)
	{
		RECT Client = {};
		::GetClientRect(Window, &Client);
		::FillRect(Dc, &Client, static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)));
	}

	void PaintSurfaceBlack(HWND Window)
	{
		if (HDC Dc = ::GetDC(Window))
		{
			PaintSurfaceBlack(Window, Dc);
			::ReleaseDC(Window, Dc);
		}
	}

	/** In front of the engine's window procedure: the engine never paints the surface, and the surface starts white and is reset when the window is resized. */
	LRESULT CALLBACK GlassWindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
	{
		if (Message == WM_ERASEBKGND)
		{
			PaintSurfaceBlack(Window, reinterpret_cast<HDC>(WParam));
			return 1;
		}
		const LRESULT Result = ::CallWindowProc(GOldWindowProc, Window, Message, WParam, LParam);
		if (Message == WM_SIZE || Message == WM_WINDOWPOSCHANGED || Message == WM_PAINT || Message == WM_DWMCOMPOSITIONCHANGED || Message == WM_SHOWWINDOW)
		{
			PaintSurfaceBlack(Window);
		}
		if (Message == WM_SIZE || Message == WM_WINDOWPOSCHANGED || Message == WM_DPICHANGED || Message == WM_SHOWWINDOW || Message == WM_NCCALCSIZE)
		{
			UpdateGlassOffset(Window);
		}
		return Result;
	}

	/** The DirectComposition objects that put the swap chain on the window; they live as long as the process. */
	struct FGlassComposition
	{
		TRefCountPtr<IDCompositionDevice> Device;
		TRefCountPtr<IDCompositionTarget> Target;
		TRefCountPtr<IDCompositionVisual> Visual;
	};
	FGlassComposition GComposition;

	/** The composition target covers the whole window, the caption and frame too, but the swap chain is the size of the client area: the visual sits where the client area starts. */
	void UpdateGlassOffset(HWND Window)
	{
		if (!GComposition.Visual.IsValid() || !GComposition.Device.IsValid())
		{
			return;
		}
		// Measured: the target's origin is already the client area, so the visual needs no offset (an offset shifted the picture by the frame size).
		(void)Window;
		GComposition.Visual->SetOffsetX(0.0f);
		GComposition.Visual->SetOffsetY(0.0f);
		GComposition.Device->Commit();
	}

	class FShiftGlassProvider final : public IDXGISwapchainProvider
	{
	public:
		virtual bool SupportsRHI(ERHIInterfaceType RHIType) const override
		{
			UE_LOG(LogShiftGlass, Log, TEXT("Glass: the RHI asks whether the provider supports RHI %d"), static_cast<int32>(RHIType));
			return RHIType == ERHIInterfaceType::D3D12;
		}
		virtual const TCHAR* GetProviderName() const override { return TEXT("Shift Happens see-through window"); }

		virtual HRESULT CreateSwapChainForHwnd(IDXGIFactory2* Factory, IUnknown* Device, HWND Window, const DXGI_SWAP_CHAIN_DESC1* Desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* FullscreenDesc,
			IDXGIOutput* RestrictToOutput, IDXGISwapChain1** OutSwapChain) override
		{
			UE_LOG(LogShiftGlass, Log, TEXT("Glass: asked for a swap chain for window %p (claimed %d, game window %d)"), Window, bClaimed ? 1 : 0, IsGameWindow(Window) ? 1 : 0);
			if (!bClaimed && IsGameWindow(Window))
			{
				bClaimed = true;   // one window: the game's
				static const bool bForceFail = FParse::Param(FCommandLine::Get(), TEXT("ShiftGlassFail"));   // QA: the fallback path
				const HRESULT Result = bForceFail ? E_FAIL : CreateGlass(Factory, Device, Window, *Desc, OutSwapChain);
				if (SUCCEEDED(Result))
				{
					GGlassActive = true;
					UE_LOG(LogShiftGlass, Display, TEXT("Glass: composition swap chain active (%ux%u, format %d, %u buffers)"), Desc->Width, Desc->Height, static_cast<int32>(Desc->Format), Desc->BufferCount);
					return Result;
				}
				UE_LOG(LogShiftGlass, Warning, TEXT("Glass: could not make the composition swap chain (0x%08x), using the normal window"), static_cast<uint32>(Result));
			}
			return Factory->CreateSwapChainForHwnd(Device, Window, Desc, FullscreenDesc, RestrictToOutput, OutSwapChain);
		}

		virtual HRESULT CreateSwapChain(IDXGIFactory* Factory, IUnknown* Device, DXGI_SWAP_CHAIN_DESC* Desc, IDXGISwapChain** OutSwapChain) override
		{
			return Factory->CreateSwapChain(Device, Desc, OutSwapChain);
		}

	private:
		static bool IsGameWindow(HWND Window)
		{
			return Window && ::IsWindow(Window) && ::GetAncestor(Window, GA_ROOT) == Window && ::GetWindow(Window, GW_OWNER) == nullptr
				&& (::GetWindowLongPtr(Window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) == 0;
		}

		static HRESULT CreateGlass(IDXGIFactory2* Factory, IUnknown* Device, HWND Window, const DXGI_SWAP_CHAIN_DESC1& Original, IDXGISwapChain1** OutSwapChain)
		{
			// the window itself is glass: its frame extends over the whole client area, so what the swap chain leaves clear shows the desktop
			const MARGINS Margins = { -1, -1, -1, -1 };
			HRESULT Result = ::DwmExtendFrameIntoClientArea(Window, &Margins);
			if (FAILED(Result))
			{
				return Result;
			}
			// What the swap chain leaves clear must show the desktop, not the window's own surface (white by default): no blur and no system backdrop material, and the
			// surface itself black, which glass shows as transparent.
			DWM_BLURBEHIND BlurBehind = {};
			BlurBehind.dwFlags = DWM_BB_ENABLE | DWM_BB_BLURREGION;
			BlurBehind.fEnable = TRUE;
			BlurBehind.hRgnBlur = ::CreateRectRgn(0, 0, -1, -1);
			::DwmEnableBlurBehindWindow(Window, &BlurBehind);
			::DeleteObject(BlurBehind.hRgnBlur);
			const int32 BackdropNone = 1;   // DWMSBT_NONE
			::DwmSetWindowAttribute(Window, 38 /* DWMWA_SYSTEMBACKDROP_TYPE */, &BackdropNone, sizeof(BackdropNone));
			PaintSurfaceBlack(Window);
			if (!GOldWindowProc)
			{
				GOldWindowProc = reinterpret_cast<WNDPROC>(::SetWindowLongPtr(Window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&GlassWindowProc)));
			}

			if (Original.Format != DXGI_FORMAT_B8G8R8A8_UNORM && Original.Format != DXGI_FORMAT_R8G8B8A8_UNORM)
			{
				UE_LOG(LogShiftGlass, Warning, TEXT("Glass: the back buffer format is %d, not 8-bit RGBA: its alpha may be too coarse for the paper opacity (r.DefaultBackBufferPixelFormat should be 0)"), static_cast<int32>(Original.Format));
			}
			DXGI_SWAP_CHAIN_DESC1 Desc = Original;
			Desc.Stereo = FALSE;
			Desc.Scaling = DXGI_SCALING_STRETCH;
			Desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
			Desc.Flags = 0;   // the proxy strips the window flags the engine passes later

			TRefCountPtr<IDXGISwapChain1> SwapChain1;
			Result = Factory->CreateSwapChainForComposition(Device, &Desc, nullptr, SwapChain1.GetInitReference());
			if (FAILED(Result))
			{
				return Result;
			}

			FGlassComposition Composition;
			Result = ::DCompositionCreateDevice(nullptr, IID_PPV_ARGS(Composition.Device.GetInitReference()));
			if (SUCCEEDED(Result))
			{
				Result = Composition.Device->CreateTargetForHwnd(Window, TRUE, Composition.Target.GetInitReference());
			}
			if (SUCCEEDED(Result))
			{
				Result = Composition.Device->CreateVisual(Composition.Visual.GetInitReference());
			}
			if (SUCCEEDED(Result))
			{
				Result = Composition.Visual->SetContent(SwapChain1.GetReference());
			}
			if (SUCCEEDED(Result))
			{
				Result = Composition.Target->SetRoot(Composition.Visual.GetReference());
			}
			if (SUCCEEDED(Result))
			{
				Result = Composition.Device->Commit();
			}
			if (FAILED(Result))
			{
				return Result;
			}
			GComposition = Composition;
			UpdateGlassOffset(Window);
			// Measured: about one launch in three never shows the swap chain (it presents fine, the desktop compositor just does not draw it) until the window frame is
			// refreshed once it has settled; a few refreshes in the first seconds make that reliable.
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Window, Count = 0](float) mutable
				{
					if (::IsWindow(Window))
					{
						::SetWindowPos(Window, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
						::RedrawWindow(Window, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
						::DwmFlush();
					}
					return ++Count < 12;
				}), 1.5f);

			// the engine wants the interface of a window swap chain: the proxy (when the system has the newest interface; every supported Windows 10 does)
			TRefCountPtr<IDXGISwapChain4> SwapChain4;
			if (SUCCEEDED(SwapChain1->QueryInterface(IID_PPV_ARGS(SwapChain4.GetInitReference()))))
			{
				TRefCountPtr<ID3D12CommandQueue> CommandQueue;
				if (Device)
				{
					Device->QueryInterface(IID_PPV_ARGS(CommandQueue.GetInitReference()));
				}
				*OutSwapChain = new FShiftGlassSwapChain(SwapChain4.GetReference(), CommandQueue.GetReference());
				return S_OK;
			}
			SwapChain1->AddRef();
			*OutSwapChain = SwapChain1.GetReference();
			return S_OK;
		}

		bool bClaimed = false;
	};

	FShiftGlassProvider GProvider;
	bool bGProviderRegistered = false;
}

#include "Windows/PostWindowsApi.h"   // last: it undefines the Windows macros (TRUE, FALSE, ...) that the code above uses

#endif   // PLATFORM_WINDOWS

bool ShiftGlass::IsRequested()
{
#if PLATFORM_WINDOWS
	const TCHAR* CommandLine = FCommandLine::Get();
	if (FParse::Param(CommandLine, TEXT("ShiftGlassProbe")))
	{
		return true;   // QA, headless: registers the provider so the log shows whether the RHI finds it (no swap chain is made off-screen)
	}
	// only the game: not the editor itself, commandlets, servers or off-screen runs (this module loads before most of the engine is up, so these are read from the command line)
	const bool bGameProcess = !WITH_EDITOR || FParse::Param(CommandLine, TEXT("game"));
	FString Ignored;
	if (!bGameProcess || IsRunningCommandlet() || IsRunningDedicatedServer() || FParse::Value(CommandLine, TEXT("run="), Ignored) || FParse::Param(CommandLine, TEXT("RenderOffScreen"))
		|| FParse::Param(CommandLine, TEXT("NullRHI")))
	{
		return false;
	}
	if (FParse::Param(CommandLine, TEXT("ShiftGlass")) || FParse::Value(CommandLine, TEXT("ShiftGlass="), Ignored))   // -ShiftGlass or -ShiftGlass=<opacity>
	{
		return true;
	}
	bool bSaved = true;   // on by default; the Settings switch writes the choice (a normal window if the system cannot make the see-through one)
	if (GConfig)
	{
		GConfig->GetBool(TEXT("ShiftHappens"), TEXT("GlassWindow"), bSaved, GGameUserSettingsIni);
	}
	return bSaved;
#else
	return false;
#endif
}

bool ShiftGlass::IsSimulated()
{
	static const bool bSimulated = []()
		{
			FString Ignored;
			return FParse::Param(FCommandLine::Get(), TEXT("ShiftGlassSim")) || FParse::Value(FCommandLine::Get(), TEXT("ShiftGlassSim="), Ignored);   // -ShiftGlassSim or -ShiftGlassSim=<opacity>
		}();
	return bSimulated;
}

bool ShiftGlass::IsActive()
{
#if PLATFORM_WINDOWS
	return GGlassActive.load() || IsSimulated();
#else
	return IsSimulated();
#endif
}

float ShiftGlass::CommandLineOpacity()
{
	FString Value;
	if ((FParse::Value(FCommandLine::Get(), TEXT("ShiftGlass="), Value) || FParse::Value(FCommandLine::Get(), TEXT("ShiftGlassSim="), Value)) && !Value.IsEmpty())
	{
		return FMath::Clamp(FCString::Atof(*Value), 0.0f, 1.0f);
	}
	return -1.0f;
}

static void RegisterGlassProvider()
{
#if PLATFORM_WINDOWS
	if (!ShiftGlass::IsRequested())
	{
		UE_LOG(LogShiftGlass, Log, TEXT("Glass: off"));
		return;
	}
	// The renderer settings the window needs, forced here for every launch (the Settings switch also writes them into the player's Engine.ini, but a Standalone or PIE session
	// started from the editor reads another config, and without them the default back buffer is 10:10:10:2, whose 2-bit alpha has only 0, 1/3, 2/3 and 1: every paper opacity
	// above about 83% rounded to solid). This module loads at PostConfigInit, before the renderer reads them: into the config the console variables are applied from, and into any
	// console variable that already exists.
	static const TCHAR* const Keys[] = { TEXT("r.PostProcessing.PropagateAlpha"), TEXT("r.TransparentViewportClear"), TEXT("r.DefaultBackBufferPixelFormat"), TEXT("r.D3D12.UseAllowTearing") };
	static const TCHAR* const Values[] = { TEXT("1"), TEXT("1"), TEXT("0"), TEXT("0") };
	for (int32 i = 0; i < 4; ++i)
	{
		if (GConfig)
		{
			GConfig->SetString(TEXT("SystemSettings"), Keys[i], Values[i], GEngineIni);
		}
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Keys[i]))
		{
			Var->Set(Values[i], ECVF_SetByCode);
		}
	}
	IModularFeatures::Get().RegisterModularFeature(IDXGISwapchainProvider::GetModularFeatureName(), &GProvider);
	bGProviderRegistered = true;
	UE_LOG(LogShiftGlass, Display, TEXT("Glass: requested, the swap chain provider is registered"));
#endif
}

static void UnregisterGlassProvider()
{
#if PLATFORM_WINDOWS
	if (bGProviderRegistered)
	{
		IModularFeatures::Get().UnregisterModularFeature(IDXGISwapchainProvider::GetModularFeatureName(), &GProvider);
		bGProviderRegistered = false;
	}
#endif
}

class FShiftGlassModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		RegisterGlassProvider();   // before the engine makes the game window: its swap chain is made then
	}

	virtual void ShutdownModule() override
	{
		UnregisterGlassProvider();
	}
};

IMPLEMENT_MODULE(FShiftGlassModule, ShiftGlass);
