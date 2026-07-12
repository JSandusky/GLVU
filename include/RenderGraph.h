#pragma once

#include <map>
#include <set>
#include <unordered_map>
#include <variant>
#include <vector>

namespace RG
{

#define GRAPH_ACCESSOR(BIND, VARNAME, TYPE) TYPE Get ## BIND() const { return VARNAME; } TYPE& BIND() { return VARNAME; } void Set ## BIND (TYPE v) { VARNAME = v; }
#ifndef GRAPH_CUSTOM_SERIAL
struct GraphSerializer {
    std::vector<unsigned char> data;
    size_t offset = 0;
    size_t size = 0;
    bool readMode = false;

    void Seek(int ofs, bool rel)
    {
        if (!rel)
            offset = (size_t)ofs;
        else
            offset += ofs;
    }

    void Write(void* d, size_t sz) {
        if (data.size() < offset + sz)
            data.resize(std::max<uint32_t>(data.size() + sz, data.size() * 0.5f));
        memcpy(data.data() + offset, &d, sz);
        offset += sz;
        size += sz;
    }

    bool Read(void* d, size_t sz)
    {
        if (offset + sz > size)
            return false;

        memcpy(d, data.data() + offset, sz);
        offset += sz;
        return true;
    }

    template<class T>
    friend GraphSerializer& operator<<(GraphSerializer&, T&);
};

template<typename T>
GraphSerializer& operator<<(GraphSerializer& s, T& v) {
    if (s.readMode)
        s.Read(&v, sizeof(T));
    else
        s.Write(&v, sizeof(T));
    return s;
}
#endif
#define GRAPH_WRITE(SRC, SZ) .Write(&SRC, SZ)
#define GRAPH_READ(DST, SZ) .Read(&DST, SZ)

#define GRAPH_COMMON_TYPEDEFS \
    typedef Pin<SOCKDATA_TYPE> PIN_T; \
    typedef Node<SOCKDATA_TYPE> NODE_T; \
    typedef Graph<SOCKDATA_TYPE> GRAPH_T; \
    typedef Pin<SOCKDATA_TYPE>* PIN_PTR; \
    typedef std::shared_ptr< Node<SOCKDATA_TYPE> > NODE_PTR; \
    typedef Graph<SOCKDATA_TYPE>* GRAPH_PTR;

template<typename SOCKDATA_TYPE>
class Node;

template<typename SOCKDATA_TYPE>
class Graph;

struct GraphGUID {
    uint32_t a;
    uint16_t b;
    uint16_t c;
    uint8_t d[8];
};

struct PinDef
{
    std::string name;
    unsigned pinID;
    unsigned pinNature;
    unsigned pinType;
    unsigned pinFlags;
};

struct NodeDef
{
    GraphGUID GUID;
    std::string name;
    std::string category;
    std::string tip;
    std::vector<PinDef> inputs;
    std::vector<PinDef> outputs;
    std::map<std::string, std::string> MetaData;

    bool SourcePins[64];
    bool TargetPins[64];
};

template<typename SOCKDATA_TYPE>
struct Pin
{
    GRAPH_COMMON_TYPEDEFS

    static constexpr unsigned PIN_DATA = 0;
    static constexpr unsigned PIN_FLOW = 1;
    static constexpr unsigned PIN_FLAG_INPUT = 1;
    static constexpr unsigned PIN_FLAG_OUTPUT = (1 << 1);

    unsigned pinID;
    uint8_t pinNature;
    unsigned pinType = 0;
    unsigned pinFlags = 0;
    NODE_PTR node;
    SOCKDATA_TYPE storedData;

    Pin() : node(nullptr), pinID(0), pinNature(0), pinFlags(0) { }

    Pin(NODE_PTR node, unsigned pinID, bool isData, bool isInput) : 
        node(node),
        pinID(pinID), 
        pinNature(isData ? PIN_DATA : PIN_FLOW), 
        pinFlags(isInput ? PIN_FLAG_INPUT : PIN_FLAG_OUTPUT) { 
    }

    inline bool IsInput() const { return pinFlags & PIN_FLAG_INPUT; }
    inline bool IsOutput() const { return pinFlags & PIN_FLAG_OUTPUT; }
    inline bool IsData() const { return pinNature & PIN_DATA; }
    inline bool IsFlow() const { return pinNature & PIN_FLOW; }

    static const bool CanConnect(const Pin& lhs, const Pin& rhs) {
        return lhs.IsInput() != rhs.IsInput() && lhs.pinType == rhs.pinType;
    }
    inline static const bool CanConnect(const Pin* lhs, const Pin* rhs) { return CanConnect(*lhs, *rhs); }

    void Serialize(GraphSerializer& str)
    {
        str << pinID << pinNature << pinType << pinFlags << storedData;
    }
};

template<typename SOCKDATA_TYPE>
class Node : public std::enable_shared_from_this< Node<SOCKDATA_TYPE> > 
{
protected:
    friend class Graph<SOCKDATA_TYPE>;
public:
    GRAPH_COMMON_TYPEDEFS

    typedef std::function<void(NODE_PTR)> NODE_VISITOR;
    typedef std::function<void(NODE_PTR, bool)> NODE_PUSH_POP;
    typedef std::function<bool(NODE_PTR, PIN_T*)> SHOULD_VISIT;

    static constexpr unsigned NODE_SIMPLE = 0;
    static constexpr unsigned NODE_BLUEPRINT = 1;
    static constexpr unsigned NODE_COMMENT = 2;

    Node() { }
    Node(unsigned nodeID, unsigned clazz) : nodeID(nodeID), nodeClass(clazz) { }

    GRAPH_ACCESSOR(NodeID, nodeID, unsigned);
    GRAPH_ACCESSOR(TypeID, nodeClass, unsigned);
    GRAPH_ACCESSOR(Data, nodeData, SOCKDATA_TYPE);
    GRAPH_ACCESSOR(VisPosX, visPos[0], float);
    GRAPH_ACCESSOR(VisPosY, visPos[1], float);
    GRAPH_ACCESSOR(VisSizeX, visSize[0], float);
    GRAPH_ACCESSOR(VisSizeY, visSize[1], float);
    GRAPH_ACCESSOR(Graph, graph, GRAPH_PTR);

    PIN_PTR GetPin(unsigned id) const {
        for (auto& i : inputs)
            if (i.pinID == id) return (PIN_PTR)&i;
        for (auto& i : outputs)
            if (i.pinID == id) return (PIN_PTR)&i;
        return nullptr;
    }

    void FollowPin(PIN_T* p, NODE_VISITOR visitor)
    {
        std::vector<PIN_T*> hooked;
        graph->GetConnections(p, hooked);
        for (auto o : hooked)
            visitor(o->node);
    }

    void Serialize(GraphSerializer& str)
    {
        unsigned iCt = inputs.size();
        unsigned oCt = outputs.size();
        str << nodeID << nodeClass << iCt << oCt << visPos[0] << visPos[1] << visSize[0] << visSize[1];

        if (str.readMode)
        {
            inputs.clear();
            outputs.clear();
            for (auto i = 0; i < iCt + oCt; ++i)
            {
                PIN_T p;
                p.node = shared_from_this();
                p.Serialize(str);
                i >= iCt ? inputs.push_back(p) : outputs.push_back(p);
            }
        }
        else
        {
            for (auto& p : inputs)
                p.Serialize(str);
            for (auto& p : outputs)
                p.Serialize(str);
        }
    }

protected:
    unsigned nodeID;
    unsigned nodeClass;
    SOCKDATA_TYPE nodeData;
    std::vector<PIN_T> inputs;
    std::vector<PIN_T> outputs;
    GRAPH_PTR graph;
    float visPos[2] = { 0.0f, 0.0f };
    float visSize[2] = { 0.0f, 0.0f };
};

template<typename SOCKDATA_TYPE>
class Graph {

public:
    GRAPH_COMMON_TYPEDEFS

    typedef std::function<void(NODE_PTR)> NODE_VISITOR;
    typedef std::function<void(NODE_PTR, bool)> NODE_PUSH_POP;
    typedef std::function<bool(NODE_PTR, PIN_T*)> SHOULD_VISIT;

    struct Link {
        unsigned outNodeID;
        unsigned outPinID;
        unsigned inNodeID;
        unsigned inPinID;
    };

    void TraceUpstream(NODE_PTR startingAt, NODE_VISITOR visitor, SHOULD_VISIT should, NODE_PUSH_POP pushPop, bool depthFirst)
    {
        std::set<NODE_PTR> visited;
        TraceUpstream(visited, startingAt, visitor, should, pushPop, depthFirst);
    }

    void TraceUpstream(NODE_PTR startingAt, NODE_VISITOR visitor, bool depthFirst)
    {
        std::set<NODE_PTR> visited;
        TraceUpstream(visited, startingAt, visitor, nullptr, nullptr, depthFirst);
    }

    void TraceUpstream(NODE_PTR startingAt, NODE_VISITOR visitor, SHOULD_VISIT should, bool depthFirst)
    {
        std::set<NODE_PTR> visited;
        TraceUpstream(visited, startingAt, visitor, should, nullptr, depthFirst);
    }

    void TraceDownstream(NODE_PTR startingAt, NODE_VISITOR visitor, SHOULD_VISIT should, NODE_PUSH_POP pushPop, bool depthFirst)
    {
        std::set<NODE_PTR> visited;
        TraceDownstream(visited, startingAt, visitor, should, pushPop, depthFirst);
    }

    void TraceDownstream(NODE_PTR startingAt, NODE_VISITOR visitor, bool depthFirst)
    {
        std::set<NODE_PTR> visited;
        TraceDownstream(visited, startingAt, visitor, nullptr, nullptr, depthFirst);
    }

    void TraceDownstream(NODE_PTR startingAt, NODE_VISITOR visitor, SHOULD_VISIT should, bool depthFirst)
    {
        std::set<NODE_PTR> visited;
        TraceDownstream(visited, startingAt, visitor, should, nullptr, depthFirst);
    }

    void Connect(NODE_T* startNode, PIN_T* startPin, NODE_T* endNode, PIN_T* endPin)
    {
        if (!PIN_T::CanConnect(startPin, endPin))
            return;

        if (startPin->IsInput())
        {
            std::swap(startNode, endNode);
            std::swap(startPin, endPin);
        }

        auto fnd = std::find_if(links.begin(), links.end(), [=](const Link& l) { return l.inPinID == startPin->pinID && l.outPinID == endPin->pinID; });
        if (fnd != links.end())
            return;

        links.push_back({ startNode->GetNodeID(), startPin->pinID, endNode->GetNodeID(), endPin->pinID });
        downstreamEdges_.insert({ startPin, endPin });
        upstreamEdges_.insert({ endPin, startPin });
    }

    void Disconnect(PIN_T* startPin, PIN_T* endPin)
    {
        if (startPin->IsInput())
            std::swap(startPin, endPin);
        
        auto fnd = std::find_if(links.begin(), links.end(), [=](const Link& l) { return l.inPinID == startPin->pinID && l.outPinID == endPin->pinID; });
        if (fnd != links.end())
        {
            links.erase(fnd);
            upstreamEdges_.erase(endPin);
            downstreamEdges_.erase(startPin);
        }
    }

    void AddNode(std::shared_ptr<NODE_T> nd) { nodes_.push_back(nd); }
    std::shared_ptr<NODE_T> FindNodeByName() const { 
        return nullptr; 
    }
    std::shared_ptr<NODE_T> FindNodeByID(unsigned id) const { 
        for (auto& n : nodes_)
            if (n->GetNodeID() == id)
                return n;
        return nullptr;
    }

    unsigned GetNextID() { return nextID++; }

    void CalculateNextID() {
        nextID = 1;
        for (auto& node : nodes_)
        {
            if (nextID <= node->NodeID()) 
                nextID = node->NodeID() + 1;
            for (auto& sock : node->inputs)
                if (nextID <= sock.pinID)
                    nextID = sock.pinID + 1;
            for (auto& sock : node->outputs)
                if (nextID <= sock.pinID)
                    nextID = sock.pinID + 1;
        }
    }

    void CompactIDs() {
        std::map<unsigned, unsigned> idTable;
        unsigned id = 1;
        for (auto& node : nodes_)
        {
            idTable[node->NodeID()] = id;
            node->NodeID() = id;
            ++id;
            for (auto& sock : node->inputs)
            {
                idTable[sock.pinID] = id;
                sock.pinID = id;
                ++id;
            }
            for (auto& sock : node->outputs)
            {
                idTable[sock.pinID] = id;
                sock.pinID = id;
                ++id;
            }
        }

        for (auto& link : links)
        {
            link.outNodeID = idTable[link.outNodeID];
            link.outPinID = idTable[link.outPinID];
            link.inNodeID = idTable[link.inNodeID];
            link.inNodeID = idTable[link.inPinID];
        }

        nextID = id;
        SetupEdges();
    }

    void PrepareGraph() { SetupEdges(); }

    void GetConnections(PIN_T* srcPin, std::vector<PIN_T*>& holder) {
        if (srcPin->IsInput())
        {
            auto range = upstreamEdges_.equal_range(srcPin);
            for (auto edge = range.first; edge != range.second; ++edge)
                holder.push_back(edge->second);
        }
        else
        {
            auto range = downstreamEdges_.equal_range(srcPin);
            for (auto edge = range.first; edge != range.second; ++edge)
                holder.push_back(edge->second);
        }
    }

    void Serialize(GraphSerializer& str)
    {
        size_t nCt = nodes_.size();
        size_t lCt = links.size();
        size_t sCt = socketData_.size();
        str << nCt << lCt << sCt;

        if (str.readMode)
        {
            nodes_.clear();
            links.clear();
            socketData_.clear();
            
            for (auto i = 0; i < nCt; ++i)
            {
                auto n = std::make_shared<NODE_T>();
                n->graph = this;
                n->Serialize(str);
                nodes_.push_back(n);
            }

            for (auto i = 0; i < lCt; ++i)
            {
                Link l;
                str << l;
                links.push_back(l);
            }

            CompactIDs();
        }
        else
        {
            for (auto n : nodes_)
                n->Serialize(str);

            for (auto l : links)
            {
                str << l;
            }
        }
    }

protected:
    void SetupEdges() {
        upstreamEdges_.clear();
        downstreamEdges_.clear();

        for (const auto& lnk : links)
        {
            auto inNode = FindNodeByID(lnk.inNodeID);
            auto outNode = FindNodeByID(lnk.outNodeID);
            if (inNode && outNode)
            {
                auto inPin = inNode->GetPin(lnk.inPinID);
                auto outPin = outNode->GetPin(lnk.outPinID);

                downstreamEdges_.insert({ outPin, inPin });
                upstreamEdges_.insert({ inPin, outPin });
            }
        }
    }

    void TraceDownstream(std::set<NODE_PTR>& visited, NODE_PTR startingAt, NODE_VISITOR visitor, SHOULD_VISIT should, NODE_PUSH_POP pushPop, bool depthFirst)
    {
        if (visited.find(startingAt) != visited.end())
            return;

        if (pushPop) pushPop(startingAt, true);
        if (!depthFirst) visitor(startingAt);
        for (auto& p : startingAt->outputs)
        {
            if (p.IsOutput() && (should && should(startingAt, &p)))
            {
                auto downstreamEdges = downstreamEdges_.equal_range(&p);
                for (auto edge = downstreamEdges.first; edge != downstreamEdges.second; ++edge)
                    TraceDownstream(visited, edge->second->node, visitor, should, pushPop, depthFirst);
            }
        }
        if (depthFirst) visitor(startingAt);
        if (pushPop) pushPop(startingAt, false);
    }
    void TraceUpstream(std::set<NODE_PTR>& visited, NODE_PTR startingAt, NODE_VISITOR visitor, SHOULD_VISIT should, NODE_PUSH_POP pushPop, bool depthFirst)
    {
        if (visited.find(startingAt) != visited.end())
            return;

        if (pushPop) pushPop(startingAt, true);
        if (!depthFirst) visitor(startingAt);
        for (auto& p : startingAt->inputs)
        {
            if (p.IsInput() && (should && should(startingAt, &p)))
            {
                auto upstreamEdges = upstreamEdges_.equal_range(&p);
                for (auto edge = upstreamEdges.first; edge != upstreamEdges.second; ++edge)
                    TraceUpstream(visited, edge->second->node, visitor, should, pushPop, depthFirst);
            }
        }
        if (depthFirst) visitor(startingAt);
        if (pushPop) pushPop(startingAt, false);
    }

    std::vector< std::shared_ptr<NODE_T> > nodes_;
    std::vector<SOCKDATA_TYPE> socketData_;
    std::vector<Link> links;
    std::unordered_multimap<PIN_T*, PIN_T*> upstreamEdges_;
    std::unordered_multimap<PIN_T*, PIN_T*> downstreamEdges_;
    unsigned nextID = 1;
};

typedef std::variant<bool, int, float, std::string> SockData;
void GraphTest() {
    Graph<SockData> graph;

    Node<SockData> nd;
    nd.SetData(0);
    
    graph.TraceUpstream(nullptr, nullptr, nullptr, nullptr, false);
    graph.TraceUpstream(nullptr, nullptr, nullptr, nullptr, true);
    graph.TraceDownstream(nullptr, nullptr, nullptr, nullptr, false);
    graph.TraceDownstream(nullptr, nullptr, nullptr, nullptr, true);

    nd.FollowPin(nd.GetPin(0), nullptr);
    GraphSerializer ser;
    graph.Serialize(ser);
    graph.Disconnect(nullptr, nullptr);
}

}
