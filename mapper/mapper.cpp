#include "mapper_pch.hpp"

namespace map_internal {
	struct graph_property {
		std::string data;

		template<class Archive>
		void serialize(Archive& ar, const unsigned int version)
		{
			(void)version;
			ar & data;
		}
	};

	struct vertex_property {
		Map::mudId_t mudId;
		std::string name;
		std::string data;
		std::tuple<int, int, int> xyz;

		template<class Archive>
		void serialize(Archive& ar, const unsigned int version)
		{
			(void)version;
			ar & mudId & name & data & std::get<0>(xyz) & std::get<1>(xyz) & std::get<2>(xyz);
		}
	};

	bool direction(std::string const& kw)
	{
		return kw == "n" || kw == "e" || kw == "s" || kw == "w" || kw == "u" || kw == "d" || kw == "ne" || kw == "se" || kw == "sw" || kw == "nw";
	}

	struct edge_property {
		std::string keyword;
		float weight = 2;
		std::string data;

		edge_property() = default;
		edge_property(std::initializer_list<std::string> il)
		{
			keyword = *(il.begin());
			if (map_internal::direction(keyword))
				weight = 1;
			else
				weight = 2;
		}

		template<class Archive>
		void serialize(Archive& ar, const unsigned int version)
		{
			ar & keyword;
			if (version >= 1)
				ar & data;
			if (map_internal::direction(keyword))
				weight = 1;
			else
				weight = 2;
		}
	};

	using mygraph_t = boost::adjacency_list<
		boost::setS, // out edges for each vertex. Set for uniqueness
		boost::vecS, // vertices
		boost::directedS,
		vertex_property,
		edge_property,
		graph_property,
		boost::vecS // edges
			>;

	size_t maybeInsert(std::string const& element, std::vector<std::string>& vec)
	{
		auto it = std::find(vec.begin(), vec.end(), element);
		if (it == vec.end())
			it = vec.emplace(it, element);
		return it - vec.begin();
	}

	std::tuple<int, int, int> coords(std::tuple<int, int, int> const& in, std::string const& dir)
	{
		int x, y, z;
		std::tie(x, y, z) = in;
		if (dir == "n")
			return std::make_tuple(x, y+1, z);
		if (dir == "e")
			return std::make_tuple(x+1, y, z);
		if (dir == "s")
			return std::make_tuple(x, y-1, z);
		if (dir == "w")
			return std::make_tuple(x-1, y, z);
		if (dir == "u")
			return std::make_tuple(x, y, z+1);
		if (dir == "d")
			return std::make_tuple(x, y, z-1);
		if (dir == "ne")
			return std::make_tuple(x+1, y+1, z);
		if (dir == "se")
			return std::make_tuple(x+1, y-1, z);
		if (dir == "sw")
			return std::make_tuple(x-1, y-1, z);
		if (dir == "nw")
			return std::make_tuple(x-1, y+1, z);
		return in;
	}

	std::vector<std::string> stackify(std::vector<mygraph_t::vertex_descriptor> const& predecessors, mygraph_t const& graph, mygraph_t::vertex_descriptor goal)
	{
		std::vector<std::string> reverse;
		for (auto v = goal;;) {
			auto pred = predecessors[v];
			if (pred == v)
				break;
			mygraph_t::edge_descriptor myEdge;
			bool found;
			std::tie(myEdge, found) = edge(predecessors[v], v, graph);
			assert(found);
			reverse.push_back(graph[myEdge].keyword);
			v = pred;
		}
		return std::vector<std::string>(reverse.crbegin(), reverse.crend());
	}
}
BOOST_CLASS_VERSION(map_internal::edge_property, 1)

using namespace map_internal;


class MapPimpl {
	public:
		mygraph_t graph;
		std::map<Map::mudId_t, mygraph_t::vertex_descriptor> ids; // mapping from arbitrary, possibly negative or stringy MUD-side IDs to small ints
};


Map::Map(std::string const& serialized)
	: d(new MapPimpl)
{
	if (serialized.empty())
		return;

	std::istringstream in(serialized);
	boost::archive::text_iarchive saved(in);
	saved >> d->graph;

	boost::graph_traits<mygraph_t>::vertex_iterator it, end;
	for (std::tie(it, end) = vertices(d->graph); it != end; ++it)
		d->ids[d->graph[*it].mudId] = *it;
}

Map::~Map()
{
	delete d;
}

std::string Map::serialize() const
{
	std::ostringstream out;
	boost::archive::text_oarchive saved(out);
	saved << d->graph;
	return out.str();
}

void Map::addRoom(Map::mudId_t room, std::string const& name, std::string const& data, std::map<std::string, Map::mudId_t> const& exits)
{
	auto vertex_it = d->ids.find(room);

	bool inserted = false;
	if (vertex_it == d->ids.end())
	{
		vertex_it = d->ids.emplace(std::make_pair(room, add_vertex(d->graph))).first;
		inserted = true;
	}
	
	auto vertex_descriptor = vertex_it->second;

	d->graph[vertex_descriptor].mudId = room;
	d->graph[vertex_descriptor].name = name;
	d->graph[vertex_descriptor].data = data;
	// only the initial room gets inserted (plus random teleports, portals and stuff).
	// The rest get added as exits.
	if (inserted)
		d->graph[vertex_descriptor].xyz = std::make_tuple(0, 0, 0);

	// Add new edges
	// Remove disappeared edges
	// Don't touch existing edges -- in particular, retain data
	for (auto exitKwDest : exits) // Add new edges
	{
		const std::string keyword = exitKwDest.first;
		const mudId_t destination = exitKwDest.second;
		auto exit_vtx_it = d->ids.find(destination);
		if (exit_vtx_it == d->ids.end())  // if exit destination doesn't exist, create it
		{
			exit_vtx_it = d->ids.emplace(destination, add_vertex(d->graph)).first;
			d->graph[exit_vtx_it->second].mudId = destination;
			d->graph[exit_vtx_it->second].xyz = coords(
					d->graph[vertex_descriptor].xyz, keyword);
		}
		// the edge with longest keyword wins (think open n;n vs n)
		auto edgeAndFound = edge(vertex_descriptor, exit_vtx_it->second, d->graph);
		if (edgeAndFound.second) {
			auto alt = edgeAndFound.first;
			if (d->graph[alt].keyword.size() < keyword.size()) {
				remove_edge(vertex_descriptor, exit_vtx_it->second, d->graph);
				assert(add_edge(vertex_descriptor, exit_vtx_it->second, {exitKwDest.first}, d->graph).second);
			}
		} else {
			assert(add_edge(vertex_descriptor, exit_vtx_it->second, {exitKwDest.first}, d->graph).second);
		}
	}

	std::vector<mygraph_t::edge_descriptor> backup; // backup because iterators get invalidated during removal
	backup.resize(boost::out_degree(vertex_descriptor, d->graph));
	auto pair = out_edges(vertex_descriptor, d->graph);
	auto it = pair.first;
	auto end = pair.second;
	std::copy(it, end, backup.begin());
	// for each edge in graph, check if its target also in our exit list. If it's not, remove it.
	for (auto edge : backup) {
		auto dest = target(edge, d->graph);
		if (std::find_if(exits.begin(), exits.end(), [this, dest](auto kwDest) { return this->d->ids[kwDest.second] == dest; }) == exits.end())
			remove_edge(vertex_descriptor, dest, d->graph);
	}
}

std::string Map::getRoomName(Map::mudId_t room) const
{
	if (d->ids.find(room) == d->ids.end())
		return "";
	return d->graph[d->ids[room]].name;
}

std::string Map::getRoomData(Map::mudId_t room) const
{
	if (d->ids.find(room) == d->ids.end())
		return "";
	return d->graph[d->ids[room]].data;
}

std::tuple<int, int, int> Map::getRoomCoords(mudId_t room) const
{
	if (d->ids.find(room) == d->ids.end())
		return std::make_tuple(0, 0, 0);
	return d->graph[d->ids[room]].xyz;
}

std::map<std::string, Map::mudId_t> Map::getRoomExits(Map::mudId_t room) const
{
	std::map<std::string, Map::mudId_t> out;
	if (d->ids.find(room) != d->ids.end()) {
		auto pair = out_edges(d->ids[room], d->graph);
		auto it = pair.first;
		auto end = pair.second;
		for (; it != end; ++it)
			out[d->graph[*it].keyword] = d->graph[target(*it, d->graph)].mudId;
	}
	return out;
}

std::vector<std::string> Map::findPath(Map::mudId_t from, Map::mudId_t to) const
{
	if (from == to)
		return {};

	if (d->ids.find(from) == d->ids.end() || d->ids.find(to) == d->ids.end())
		return {};

	std::vector<mygraph_t::vertex_descriptor> predecessors(num_vertices(d->graph));
	auto vertex = d->ids[from];
	auto goal = d->ids[to];
	int x1, y1, z1;
	std::tie(x1, y1, z1) = d->graph[vertex].xyz;
	auto heuristic = [this, x1, y1, z1](mygraph_t::vertex_descriptor vtx) {
		int x2, y2, z2;
		std::tie(x2, y2, z2) = d->graph[vtx].xyz;
		auto dx = x2 - x1;
		auto dy = y2 - y1;
		auto dz = z2 - z1;
		return ::sqrt(dx*dx + dy*dy + dz*dz);
	};

	// visitor that terminates when we find the goal
	struct found_goal {};
	class astar_goal_visitor : public boost::default_astar_visitor
	{
		public:
			astar_goal_visitor(mygraph_t::vertex_descriptor goal)
				: m_goal(goal)
			{}
			void examine_vertex(const mygraph_t::vertex_descriptor u, const mygraph_t& graph) const {
				(void)graph;
				if (u == m_goal)
					throw found_goal();
			}
		private:
			mygraph_t::vertex_descriptor m_goal;
	};

	try {
		std::vector<int> distances(num_vertices(d->graph));
		std::vector<int> costs(num_vertices(d->graph));
		astar_search(d->graph, vertex, heuristic,
				boost::predecessor_map(make_iterator_property_map(predecessors.begin(), get(boost::vertex_index, d->graph))).
				distance_map(make_iterator_property_map(distances.begin(), get(boost::vertex_index, d->graph))).
				weight_map(get(&edge_property::weight, d->graph)).
				visitor(astar_goal_visitor(goal))
				);
	} catch(const found_goal&) {
		return stackify(predecessors, d->graph, goal);
	}

	return {};
}

std::map<Map::mudId_t, std::string> Map::findRoomByName(std::string const& name) const
{
	std::map<Map::mudId_t, std::string> out;

	boost::graph_traits<mygraph_t>::vertex_iterator vi, vi_end;
	for (std::tie(vi, vi_end) = vertices(d->graph); vi != vi_end; ++vi) {
		if (d->graph[*vi].name.find(name) != std::string::npos)
			out[d->graph[*vi].mudId] = d->graph[*vi].name;
	}
	return out;
}

void Map::setRoomData(mudId_t room, std::string const& data)
{
	auto vtx = d->ids[room];
	d->graph[vtx].data = data;
}

void Map::setMapData(std::string const& data)
{
	d->graph[boost::graph_bundle].data = data;
}

std::string Map::getMapData() const
{
	return d->graph[boost::graph_bundle].data;
}

std::string Map::getExitData(Map::mudId_t source, Map::mudId_t target) const
{
	auto edge_found = edge(d->ids[source], d->ids[target], d->graph);
	return edge_found.second ? d->graph[edge_found.first].data : "";
}

void Map::setExitData(Map::mudId_t source, Map::mudId_t target, std::string const& data)
{
	auto edge_found = edge(d->ids[source], d->ids[target], d->graph);
	if (edge_found.second)
		d->graph[edge_found.first].data = data;
}
