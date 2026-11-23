// Shift Happens - Game Off 2026

#include "Audio/Nodes/ShiftComposite.h"

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/Platform.h"
#include <initializer_list>
#include "Logging/LogMacros.h"
#include "MetasoundBuilderInterface.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendNodeClassRegistry.h"
#include "MetasoundFrontendRegistryKey.h"
#include "MetasoundGraph.h"
#include "MetasoundLiteral.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorBuilder.h"
#include "MetasoundOperatorBuilderSettings.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundOutputNode.h"
#include "MetasoundVertex.h"
#include "MetasoundVertexData.h"
#include "Misc/Guid.h"
#include "ShiftHappens.h"
#include "Templates/UniquePtr.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/NameTypes.h"

namespace ShiftAudio
{
	using namespace Metasound;

	FGraphBuilder::FGraphBuilder(const TCHAR* InName)
		: Graph(MakeUnique<FGraph>(FName(InName), FGuid::NewGuid()))
	{
	}

	const INode* FGraphBuilder::Add(const TCHAR* Namespace, const TCHAR* Name, const TCHAR* Variant, int32 Major)
	{
		Frontend::INodeClassRegistry& Registry = Frontend::INodeClassRegistry::GetChecked();
		const FMetasoundFrontendClassName ClassName{ FName(Namespace), FName(Name), FName(Variant) };
		for (int32 Minor = 9; Minor >= 0; --Minor)
		{
			const Frontend::FNodeClassRegistryKey Key(EMetasoundFrontendClassType::External, ClassName, Major, Minor);
			FVertexInterface Interface;
			if (!Registry.FindDefaultVertexInterface(Key, Interface))
			{
				continue;
			}
			const FGuid Id = FGuid::NewGuid();
			TUniquePtr<INode> Node = Registry.CreateNode(Key, FNodeData(FName(*FString::Printf(TEXT("%s_%d"), Name, Count)), Id, MoveTemp(Interface)));
			if (!Node.IsValid())
			{
				break;
			}
			const INode* Raw = Node.Get();
			Graph->AddNode(Id, MoveTemp(Node));
			++Count;
			return Raw;
		}
		UE_LOG(LogShift, Error, TEXT("Composite graph: node %s :: %s :: %s (major %d) is not registered"), Namespace, Name, Variant, Major);
		return nullptr;
	}

	const INode* FGraphBuilder::Input(const TCHAR* VertexName, const FName& DataType, const FLiteral& Default)
	{
		Frontend::FNodeClassRegistryKey Key;
		Frontend::INodeClassRegistry& Registry = Frontend::INodeClassRegistry::GetChecked();
		if (!Registry.FindInputNodeRegistryKeyForDataType(DataType, EMetasoundFrontendVertexAccessType::Reference, Key))
		{
			UE_LOG(LogShift, Error, TEXT("Composite graph: no input node for data type %s"), *DataType.ToString());
			return nullptr;
		}
		// the interface of a graph input node: one input and one output vertex, both named like the graph input
		FVertexInterface Interface(
			FInputVertexInterface(FInputDataVertex(FVertexName(VertexName), DataType, FDataVertexMetadata{}, EVertexAccessType::Reference)),
			FOutputVertexInterface(FOutputDataVertex(FVertexName(VertexName), DataType, FDataVertexMetadata{}, EVertexAccessType::Reference)));
		const FGuid Id = FGuid::NewGuid();
		TUniquePtr<INode> Node = Registry.CreateNode(Key, FNodeData(FName(VertexName), Id, MoveTemp(Interface)));
		if (!Node.IsValid())
		{
			return nullptr;
		}
		const INode* Raw = Node.Get();
		Graph->AddInputNode(Id, FVertexName(VertexName), MoveTemp(Node));
		// The first vertex, without a loop that always breaks: the optimizer reports its increment as unreachable code (C4702, an error in UE builds).
		const FInputVertexInterface& Inputs = Raw->GetVertexInterface().GetInputInterface();
		if (auto First = Inputs.begin(); First != Inputs.end())
		{
			Graph->SetNodeDefaultInput(Id, (*First).VertexName, Default);
		}
		return Raw;
	}

	const INode* FGraphBuilder::Output(const TCHAR* VertexName, const FName& DataType)
	{
		Frontend::FNodeClassRegistryKey Key;
		Frontend::INodeClassRegistry& Registry = Frontend::INodeClassRegistry::GetChecked();
		if (!Registry.FindOutputNodeRegistryKeyForDataType(DataType, EMetasoundFrontendVertexAccessType::Reference, Key))
		{
			UE_LOG(LogShift, Error, TEXT("Composite graph: no output node for data type %s"), *DataType.ToString());
			return nullptr;
		}
		FVertexInterface Interface = FOutputNode::CreateVertexInterface(FVertexName(VertexName), DataType, EVertexAccessType::Reference);
		const FGuid Id = FGuid::NewGuid();
		TUniquePtr<INode> Node = Registry.CreateNode(Key, FNodeData(FName(VertexName), Id, MoveTemp(Interface)));
		if (!Node.IsValid())
		{
			return nullptr;
		}
		const INode* Raw = Node.Get();
		Graph->AddOutputNode(Id, FVertexName(VertexName), MoveTemp(Node));
		return Raw;
	}

	void FGraphBuilder::Set(const INode* Node, const TCHAR* Pin, const FLiteral& Literal)
	{
		if (Node)
		{
			const_cast<INode*>(Node)->SetDefaultInput(FVertexName(Pin), Literal);
		}
	}

	bool FGraphBuilder::Connect(const FPin& From, const INode* To, const TCHAR* ToPin)
	{
		if (!From.Node || !To)
		{
			return false;
		}
		const bool bOk = Graph->AddDataEdge(*From.Node, From.Name, *To, FVertexName(ToPin));
		if (!bOk)
		{
			UE_LOG(LogShift, Error, TEXT("Composite graph: cannot connect %s to %s"), *From.Name.ToString(), ToPin);
		}
		return bOk;
	}

	bool FGraphBuilder::Connect(const FPin& From, const INode* To, const FString& ToPin)
	{
		return Connect(From, To, *ToPin);
	}

	FPin FGraphBuilder::Source(const INode* InputNode)
	{
		FPin Result;
		Result.Node = InputNode;
		const FOutputVertexInterface& Outputs = InputNode->GetVertexInterface().GetOutputInterface();
		if (auto First = Outputs.begin(); First != Outputs.end())
		{
			Result.Name = (*First).VertexName;
		}
		return Result;
	}

	bool FGraphBuilder::ConnectToOutput(const FPin& From, const INode* OutputNode)
	{
		if (!From.Node || !OutputNode)
		{
			return false;
		}
		const FInputVertexInterface& Inputs = OutputNode->GetVertexInterface().GetInputInterface();
		auto First = Inputs.begin();
		return First != Inputs.end() && Graph->AddDataEdge(*From.Node, From.Name, *OutputNode, (*First).VertexName);
	}

	FPin FGraphBuilder::Out(const INode* Node, const TCHAR* Pin)
	{
		FPin Result;
		Result.Node = Node;
		Result.Name = FVertexName(Pin);
		return Result;
	}

	namespace
	{
		/** Runs the engine's graph operator and keeps the graph it was built from alive. */
		class FOwningGraphOperator : public IOperator
		{
		public:
			FOwningGraphOperator(TUniquePtr<FGraph> InGraph, TUniquePtr<IOperator> InInner)
				: Graph(MoveTemp(InGraph))
				, Inner(MoveTemp(InInner))
			{
			}

			virtual void BindInputs(FInputVertexInterfaceData& InVertexData) override { Inner->BindInputs(InVertexData); }
			virtual void BindOutputs(FOutputVertexInterfaceData& InVertexData) override { Inner->BindOutputs(InVertexData); }

			virtual FResetFunction GetResetFunction() override
			{
				return [](IOperator* Op, const FResetParams& Params)
					{
						IOperator* Inner = static_cast<FOwningGraphOperator*>(Op)->Inner.Get();
						if (FResetFunction F = Inner->GetResetFunction())
						{
							F(Inner, Params);
						}
					};
			}

			virtual FExecuteFunction GetExecuteFunction() override
			{
				return [](IOperator* Op)
					{
						IOperator* Inner = static_cast<FOwningGraphOperator*>(Op)->Inner.Get();
						if (FExecuteFunction F = Inner->GetExecuteFunction())
						{
							F(Inner);
						}
					};
			}

			virtual FPostExecuteFunction GetPostExecuteFunction() override
			{
				return [](IOperator* Op)
					{
						IOperator* Inner = static_cast<FOwningGraphOperator*>(Op)->Inner.Get();
						if (FPostExecuteFunction F = Inner->GetPostExecuteFunction())
						{
							F(Inner);
						}
					};
			}

		private:
			TUniquePtr<FGraph> Graph;
			TUniquePtr<IOperator> Inner;
		};
	}

	bool FGraphPart::Build(TUniquePtr<FGraph> InGraph, std::initializer_list<FLink> Links, const FBuildOperatorParams& InParams, FBuildResults& OutResults)
	{
		return BuildFrom(MoveTemp(InGraph), Links.begin(), static_cast<int32>(Links.size()), InParams, OutResults);
	}

	bool FGraphPart::Build(TUniquePtr<FGraph> InGraph, const TArray<FLink>& Links, const FBuildOperatorParams& InParams, FBuildResults& OutResults)
	{
		return BuildFrom(MoveTemp(InGraph), Links.GetData(), Links.Num(), InParams, OutResults);
	}

	bool FGraphPart::BuildFrom(TUniquePtr<FGraph> InGraph, const FLink* Links, int32 NumLinks, const FBuildOperatorParams& InParams, FBuildResults& OutResults)
	{
		Graph = MoveTemp(InGraph);
		FInputVertexInterfaceData Inputs(Graph->GetVertexInterface().GetInputInterface());
		for (int32 i = 0; i < NumLinks; ++i)
		{
			Inputs.SetVertex(Links[i].Name, Links[i].Ref);
		}
		FOperatorBuilder Builder(FOperatorBuilderSettings::GetDefaultSettings());
		const FBuildGraphOperatorParams GraphParams(*Graph, InParams.OperatorSettings, Inputs, InParams.Environment, InParams.GraphRenderCost);
		Op = Builder.BuildGraphOperator(GraphParams, OutResults);
		if (!Op.IsValid())
		{
			UE_LOG(LogShift, Error, TEXT("Composite graph: the engine could not build the operator of a graph part"));
			return false;
		}
		Outputs = FOutputVertexInterfaceData(Graph->GetVertexInterface().GetOutputInterface());
		Op->BindOutputs(Outputs);
		ExecuteFunction = Op->GetExecuteFunction();
		PostExecuteFunction = Op->GetPostExecuteFunction();
		ResetFunction = Op->GetResetFunction();
		return true;
	}

	TUniquePtr<IOperator> BuildOwningGraphOperator(TUniquePtr<FGraph> Graph, const FBuildOperatorParams& InParams, FBuildResults& OutResults)
	{
		FOperatorBuilder Builder(FOperatorBuilderSettings::GetDefaultSettings());
		const FBuildGraphOperatorParams GraphParams(*Graph, InParams.OperatorSettings, InParams.InputData, InParams.Environment, InParams.GraphRenderCost);
		TUniquePtr<IOperator> Inner = Builder.BuildGraphOperator(GraphParams, OutResults);
		if (!Inner.IsValid())
		{
			UE_LOG(LogShift, Error, TEXT("Composite graph: the engine could not build the graph operator"));
			return nullptr;
		}
		return MakeUnique<FOwningGraphOperator>(MoveTemp(Graph), MoveTemp(Inner));
	}
}
