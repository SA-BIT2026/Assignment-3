/**
 * CFLR.cpp
 * @author kisslune 
 */

#include "A4Header.h"

using namespace SVF;
using namespace llvm;
using namespace std;

int main(int argc, char **argv)
{
    auto moduleNameVec =
            OptionBase::parseOptions(argc, argv, "Whole Program Points-to Analysis",
                                     "[options] <input-bitcode...>");

    LLVMModuleSet::buildSVFModule(moduleNameVec);

    SVFIRBuilder builder;
    auto pag = builder.build();
    pag->dump();

    CFLR solver;
    solver.buildGraph(pag);
    solver.solve();
    solver.dumpResult();

    LLVMModuleSet::releaseLLVMModuleSet();
    return 0;
}


void CFLR::solve()
{
    if (!graph)
        return;

    // Lec04, p.50: A ::= B C. Bar labels denote reversed relations.
    struct BinaryRule { EdgeLabel lhs, first, second; };
    const BinaryRule rules[] = {
        {PT, VFBar, AddrBar},
        {PTBar, Addr, VF},
        {VF, VF, VF},
        {VF, SV, Load},
        {VF, PV, Load},
        {VF, Store, VP},
        {VFBar, VFBar, VFBar},
        {VFBar, LoadBar, SVBar},
        {VFBar, LoadBar, VP},
        {VFBar, PV, StoreBar},
        {VA, LV, Load},
        {VA, VFBar, VA},
        {VA, VA, VF},
        {SV, Store, VA},
        {SVBar, VA, StoreBar},
        {PV, PTBar, VA},
        {VP, VA, PT},
        {LV, LoadBar, VA},
    };

    workList.clear();
    for (const auto &src : graph->getSuccessorMap())
        for (const auto &label : src.second)
            for (unsigned dst : label.second)
                workList.push(CFLREdge(src.first, dst, label.first));

    // Add to graph immediately; enqueue only facts not previously discovered.
    const auto add = [this](unsigned src, unsigned dst, EdgeLabel label) {
        if (!graph->hasEdge(src, dst, label))
        {
            graph->addEdge(src, dst, label);
            workList.push(CFLREdge(src, dst, label));
        }
    };

    // VF ::= epsilon, VFBar ::= epsilon, VA ::= epsilon.
    for (unsigned node : graph->getNodes())
        for (EdgeLabel label : {VF, VFBar, VA})
            add(node, node, label);

    // Return a snapshot: inferred edges can rehash the adjacency maps/sets.
    const auto neighbors = [](const CFLRGraph::DataMap &map,
                               unsigned node, EdgeLabel label) {
        std::vector<unsigned> result;
        const auto nodeIt = map.find(node);
        if (nodeIt != map.end())
        {
            const auto labelIt = nodeIt->second.find(label);
            if (labelIt != nodeIt->second.end())
                result.assign(labelIt->second.begin(), labelIt->second.end());
        }
        return result;
    };

    while (!workList.empty())
    {
        const CFLREdge edge = workList.pop();
        // Unary productions VF ::= Copy and VFBar ::= CopyBar.
        if (edge.label == Copy)
            add(edge.src, edge.dst, VF);
        else if (edge.label == CopyBar)
            add(edge.src, edge.dst, VFBar);

        for (const auto &rule : rules)
        {
            // New edge is the left operand: x -B-> y -C-> z.
            if (edge.label == rule.first)
                for (unsigned dst : neighbors(graph->getSuccessorMap(), edge.dst, rule.second))
                    add(edge.src, dst, rule.lhs);

            // New edge is the right operand: x -B-> y -C-> z.
            if (edge.label == rule.second)
                for (unsigned src : neighbors(graph->getPredecessorMap(), edge.src, rule.first))
                    add(src, edge.dst, rule.lhs);
        }
    }
}
