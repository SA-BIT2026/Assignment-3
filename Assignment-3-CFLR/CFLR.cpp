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
    // The graph holds four kinds of terminal edges: Addr (obj -> ptr, i.e. p = &x),
    // Copy (value flow), Load (ptr -> the value loaded from *ptr) and Store (value ->
    // the ptr it is stored through). Points-to is computed as CFL-reachability with
    // the following productions, where PT(x,p) means x in pts(p) and LV(x,p) means
    // x in pts(*p):
    //
    //   Addr -> PT          p = &x                     =>  x in pts(p)
    //   PT Copy -> PT       q = p                      =>  pts(q) >= pts(p)
    //   PT PT -> LV         x in pts(o), o in pts(p)   =>  x in pts(*p)
    //   LV Copy -> LV       q = p                      =>  pts(*q) >= pts(*p)
    //   LV Load -> PT       q = *p                     =>  pts(q) >= pts(*p)
    //   PT Store -> LV      *loc = v                   =>  pts(*loc) >= pts(v)
    //   LV PTBar -> PT      x in pts(*loc), o in pts(loc)  =>  x in pts(o)
    //
    // The last production needs the PT edge (o,loc) walked backwards, which is why
    // the predecessors map is consulted for it.

    auto &succ = graph->getSuccessorMap();
    auto &pred = graph->getPredecessorMap();

    // Copies are returned on purpose: deriving new edges mutates the maps.
    auto outEdges = [&](unsigned node, EdgeLabel label)
    {
        std::unordered_set<unsigned> res;
        auto nodeItr = succ.find(node);
        if (nodeItr != succ.end())
        {
            auto labelItr = nodeItr->second.find(label);
            if (labelItr != nodeItr->second.end())
                res = labelItr->second;
        }
        return res;
    };

    auto inEdges = [&](unsigned node, EdgeLabel label)
    {
        std::unordered_set<unsigned> res;
        auto nodeItr = pred.find(node);
        if (nodeItr != pred.end())
        {
            auto labelItr = nodeItr->second.find(label);
            if (labelItr != nodeItr->second.end())
                res = labelItr->second;
        }
        return res;
    };

    auto derive = [&](unsigned src, EdgeLabel label, unsigned dst)
    {
        if (graph->hasEdge(src, dst, label))
            return;
        graph->addEdge(src, dst, label);
        workList.push(CFLREdge(src, dst, label));
    };

    for (auto &nodeItr : succ)
        for (auto &labelItr : nodeItr.second)
            for (auto dst : labelItr.second)
                workList.push(CFLREdge(nodeItr.first, dst, labelItr.first));

    while (!workList.empty())
    {
        CFLREdge edge = workList.pop();
        unsigned src = edge.src;
        unsigned dst = edge.dst;

        switch (edge.label)
        {
            case Addr:
                derive(src, PT, dst);
                break;

            case PT:
                for (auto next : outEdges(dst, Copy))
                    derive(src, PT, next);
                for (auto next : outEdges(dst, Store))
                    derive(src, LV, next);
                for (auto next : outEdges(dst, PT))
                    derive(src, LV, next);
                for (auto prev : inEdges(src, PT))
                    derive(prev, LV, dst);
                for (auto prev : inEdges(dst, LV))
                    derive(prev, PT, src);
                break;

            case LV:
                for (auto next : outEdges(dst, Copy))
                    derive(src, LV, next);
                for (auto next : outEdges(dst, Load))
                    derive(src, PT, next);
                for (auto prev : inEdges(dst, PT))
                    derive(src, PT, prev);
                break;

            default:
                // Copy, Store and Load terminals only appear as the second symbol
                // of a production; those firings are covered by the cases above.
                break;
        }
    }
}
