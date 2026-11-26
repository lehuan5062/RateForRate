// Rate for Rate - Game Off 2026

#pragma once

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "HAL/Platform.h"
#include <initializer_list>
#include "MetasoundBuilderInterface.h"
#include "MetasoundDataReference.h"
#include "MetasoundGraph.h"
#include "MetasoundLiteral.h"
#include "MetasoundNodeInterface.h"
#include "MetasoundOperatorInterface.h"
#include "MetasoundVertex.h"
#include "MetasoundVertexData.h"
#include "Templates/UniquePtr.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/NameTypes.h"

/**
 * Composite MetaSound nodes: a whole MetaSound graph of ordinary engine and Harmonix nodes, assembled in C++ and shown in an asset as ONE node.
 * Nothing is re-implemented: every piece is the engine's own node, created from the node registry by class name, wired with data edges
 * (the C++ counterpart of Tools/py/ms_graph.py). The composite builds the inner graph when its operator is created and hands the real
 * graph operator to the MetaSound generator.
 */
namespace ShiftAudio
{
	/** One output of a node in the graph being built. */
	struct FPin
	{
		const Metasound::INode* Node = nullptr;
		Metasound::FVertexName Name;
	};

	class FGraphBuilder
	{
	public:
		explicit FGraphBuilder(const TCHAR* InName);

		/** The finished graph (give it to FOperatorBuilder, keep it alive as long as the operator lives). */
		TUniquePtr<Metasound::FGraph> Release() { return MoveTemp(Graph); }

		/** A node of a registered class (the highest minor version of that major version); nullptr, with an error in the log, when it is not registered. */
		const Metasound::INode* Add(const TCHAR* Namespace, const TCHAR* Name, const TCHAR* Variant, int32 Major);

		/** A graph input / output of a data type (the MetaSound data type name, e.g. "Float", "Trigger", "Audio", "MIDIClock"). */
		const Metasound::INode* Input(const TCHAR* VertexName, const FName& DataType, const Metasound::FLiteral& Default);
		const Metasound::INode* Output(const TCHAR* VertexName, const FName& DataType);

		void Set(const Metasound::INode* Node, const TCHAR* Pin, const Metasound::FLiteral& Literal);
		bool Connect(const FPin& From, const Metasound::INode* To, const TCHAR* ToPin);
		bool Connect(const FPin& From, const Metasound::INode* To, const FString& ToPin);

		static FPin Out(const Metasound::INode* Node, const TCHAR* Pin);

		/** The only output of a graph input node, and a connection to the only input of a graph output node. */
		static FPin Source(const Metasound::INode* InputNode);
		bool ConnectToOutput(const FPin& From, const Metasound::INode* OutputNode);

		int32 NodeCount() const { return Count; }

	private:
		TUniquePtr<Metasound::FGraph> Graph;
		int32 Count = 0;
	};

	/**
	 * Base of a composite's operator class. The node facade wants an IOperator-derived type for its static functions (GetNodeInfo, GetVertexInterface,
	 * CreateOperator); the composite never makes an instance of it, CreateOperator hands out the inner graph's own operator.
	 */
	struct FCompositeOperatorBase : public Metasound::IOperator
	{
		virtual void BindInputs(Metasound::FInputVertexInterfaceData&) override {}
		virtual void BindOutputs(Metasound::FOutputVertexInterfaceData&) override {}
		virtual FResetFunction GetResetFunction() override { return nullptr; }
		virtual FExecuteFunction GetExecuteFunction() override { return nullptr; }
		virtual FPostExecuteFunction GetPostExecuteFunction() override { return nullptr; }
	};

	/** An input of a graph part that is fed by a data reference of the composite or of another part. */
	struct FLink
	{
		Metasound::FVertexName Name;
		Metasound::FAnyDataReference Ref;
	};

	/**
	 * One engine graph of a composite that runs apart from the others: the composite's operator runs its parts itself, in an order it chooses, and may leave a
	 * part out for a block (a silent layer). Its inputs are the data references of the composite or the outputs of the parts that ran before it; its outputs are
	 * read back with Output(). Still the engine's own graph operator inside: the composite only decides which one executes.
	 */
	class FGraphPart
	{
	public:
		bool Build(TUniquePtr<Metasound::FGraph> InGraph, std::initializer_list<FLink> Links, const Metasound::FBuildOperatorParams& InParams, Metasound::FBuildResults& OutResults);
		bool Build(TUniquePtr<Metasound::FGraph> InGraph, const TArray<FLink>& Links, const Metasound::FBuildOperatorParams& InParams, Metasound::FBuildResults& OutResults);

		const Metasound::FAnyDataReference* Output(const TCHAR* Name) const { return Outputs.FindDataReference(Metasound::FVertexName(Name)); }

		void Execute() { if (ExecuteFunction) { ExecuteFunction(Op.Get()); } }
		void PostExecute() { if (PostExecuteFunction) { PostExecuteFunction(Op.Get()); } }
		void Reset(const Metasound::IOperator::FResetParams& Params) { if (ResetFunction) { ResetFunction(Op.Get(), Params); } }

	private:
		bool BuildFrom(TUniquePtr<Metasound::FGraph> InGraph, const FLink* Links, int32 NumLinks, const Metasound::FBuildOperatorParams& InParams, Metasound::FBuildResults& OutResults);

		TUniquePtr<Metasound::FGraph> Graph;
		TUniquePtr<Metasound::IOperator> Op;
		Metasound::FOutputVertexInterfaceData Outputs;
		Metasound::IOperator::FExecuteFunction ExecuteFunction = nullptr;
		Metasound::IOperator::FPostExecuteFunction PostExecuteFunction = nullptr;
		Metasound::IOperator::FResetFunction ResetFunction = nullptr;
	};

	/**
	 * Builds the real operator of a composite: the graph the recipe assembles, run by the engine's own graph operator builder. The returned operator
	 * owns the graph.
	 */
	TUniquePtr<Metasound::IOperator> BuildOwningGraphOperator(TUniquePtr<Metasound::FGraph> Graph, const Metasound::FBuildOperatorParams& InParams, Metasound::FBuildResults& OutResults);
}
