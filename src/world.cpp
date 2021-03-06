#include "world.h"

#include "contiguous_hashmap.h"
#include "chunk.h"
#include "chunkdata.h"
#include "messaging.h"
#include "minichunkmesh.h"
#include "render.h"
#include "shapes.h"
#include "util.h"

#include "vmath.h"
#include "zmq_addon.hpp"

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <queue>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <sstream>
#include <string>

// radius from center of minichunk that must be included in view frustum
constexpr float FRUSTUM_MINI_RADIUS_ALLOWANCE = 28.0f;

WorldDataPart::WorldDataPart(std::shared_ptr<zmq::context_t> ctx_) : bus(ctx_)
{
#ifdef _DEBUG
	bus.out.setsockopt(ZMQ_SUBSCRIBE, "", 0);
#else
	for (const auto& m : msg::world_thread_incoming)
	{
		// TODO: Upgrade zmq and replace this with .set()
		bus.out.setsockopt(ZMQ_SUBSCRIBE, m.c_str(), m.size());
	}
#endif // _DEBUG
}

// update tick to *new_tick*
void WorldDataPart::update_tick(const int new_tick) {
	// can only grow, not shrink
	if (new_tick <= current_tick) {
		return;
	}

	current_tick = new_tick;

	// propagate any water we need to propagate
	while (!water_propagation_queue.empty()) {
		// get item from queue
		const auto& [tick, xyz] = water_propagation_queue.top();

		// if tick is in the future, ignore
		if (tick > current_tick) {
			break;
		}

		// do it
		water_propagation_queue.pop();
		propagate_water(xyz[0], xyz[1], xyz[2]);
	}
}

// enqueue mesh generation of this mini
// expects mesh lock
void WorldDataPart::enqueue_mesh_gen(std::shared_ptr<MiniChunk> mini, const bool front_of_queue) {
	assert(mini != nullptr && "seriously?");

	// check if mini in set
	MeshGenRequest* req = new MeshGenRequest();
	req->coords = mini->get_coords();
	req->data = std::make_shared<MeshGenRequestData>();
	req->data->self = mini;

#define ADD(ATTR, DIRECTION)\
		{\
			std::shared_ptr<MiniChunk> minip_ = get_mini(mini->get_coords() + DIRECTION);\
			req->data->ATTR = minip_;\
		}

	ADD(up, IUP * 16);
	ADD(down, IDOWN * 16);
	ADD(east, IEAST);
	ADD(west, IWEST);
	ADD(north, INORTH);
	ADD(south, ISOUTH);
#undef ADD

	// TODO: Figure out how to do zero-copy messaging since we don't need to copy msg::MESH_GEN_REQ (it's static const)
	std::vector<zmq::const_buffer> message({
		zmq::buffer(msg::MESH_GEN_REQUEST),
		zmq::buffer(&req, sizeof(req))
		});

	auto ret = zmq::send_multipart(bus.in, message, zmq::send_flags::dontwait);
	assert(ret);
}

// add chunk to chunk coords (x, z)
void WorldDataPart::add_chunk(const int x, const int z, std::shared_ptr<Chunk> chunk) {
	const vmath::ivec2 coords = { x, z };
	const auto search = chunk_map.find(coords);

	// if element already exists, error
	if (search != chunk_map.end()) {
		throw "Tried to add chunk but it already exists.";
	}

	// insert our chunk
	if (chunk == nullptr) {
		throw "Wew";
	}
	chunk_map[coords] = chunk;
}

// generate chunks if they don't exist yet
void WorldDataPart::gen_chunks_if_required(const vector<vmath::ivec2>& chunk_coords) {
	// don't wanna generate duplicates
	std::unordered_set<vmath::ivec2, vecN_hash> to_generate;

	for (auto coords : chunk_coords) {
		const auto search = chunk_map.find(coords);

		// if doesn't exist, need to generate it
		if (search == chunk_map.end()) {
			to_generate.insert(coords);
		}
	}

	if (to_generate.size() > 0) {
		gen_chunks(to_generate);
	}
}

// generate multiple chunks
void WorldDataPart::gen_chunks(const std::unordered_set<vmath::ivec2, vecN_hash>& to_generate) {
	// Instead of generating chunks ourselves, we request the ChunkGenThread to do it for us.
	// TODO: Send one request with a vector of coords?
	for (const vmath::ivec2& coords : to_generate)
	{
		ChunkGenRequest* req = new ChunkGenRequest;
		req->coords = coords;
		std::vector<zmq::const_buffer> message({
			zmq::buffer(msg::CHUNK_GEN_REQUEST),
			zmq::buffer(&req, sizeof(req))
			});

		auto ret = zmq::send_multipart(bus.in, message, zmq::send_flags::dontwait);
		assert(ret);
	}

	return;
}

// get chunk or nullptr (using cache) (TODO: LRU?)
std::shared_ptr<Chunk> WorldDataPart::get_chunk(const int x, const int z) {
	const auto search = chunk_map.find({ x, z });

	// if doesn't exist, return null
	if (search == chunk_map.end()) {
		return nullptr;
	}

	return search->second;
}

std::shared_ptr<Chunk> WorldDataPart::get_chunk(const vmath::ivec2& xz) { return get_chunk(xz[0], xz[1]); }

// get mini or nullptr
std::shared_ptr<MiniChunk> WorldDataPart::get_mini(const int x, const int y, const int z) {
	const auto search = chunk_map.find({ x, z });

	// if chunk doesn't exist, return null
	if (search == chunk_map.end()) {
		return nullptr;
	}

	std::shared_ptr<Chunk> chunk = search->second;
	return chunk->get_mini_with_y_level((y / 16) * 16); // TODO: Just y % 16?
}

std::shared_ptr<MiniChunk> WorldDataPart::get_mini(const vmath::ivec3& xyz) { return get_mini(xyz[0], xyz[1], xyz[2]); }

// generate chunks near player
void WorldDataPart::gen_nearby_chunks(const vmath::vec4& position, const int& distance) {
	assert(distance >= 0 && "invalid distance");

	const vmath::ivec2 chunk_coords = get_chunk_coords(position[0], position[2]);
	const vector<vmath::ivec2> coords = gen_circle(distance, chunk_coords);

	gen_chunks_if_required(coords);
}

// get chunk that contains block at (x, _, z)
std::shared_ptr<Chunk> WorldDataPart::get_chunk_containing_block(const int x, const int z) {
	return get_chunk((int)floorf(static_cast<float>(x) / 16.0f), (int)floorf(static_cast<float>(z) / 16.0f));
}

// get minichunk that contains block at (x, y, z)
std::shared_ptr<MiniChunk> WorldDataPart::get_mini_containing_block(const int x, const int y, const int z) {
	std::shared_ptr<Chunk> chunk = get_chunk_containing_block(x, z);
	if (chunk == nullptr) {
		return nullptr;
	}
	return chunk->get_mini_with_y_level((y / 16) * 16);
}


// get minichunks that touch any face of the block at (x, y, z)
std::vector<std::shared_ptr<MiniChunk>> WorldDataPart::get_minis_touching_block(const int x, const int y, const int z) {
	vector<std::shared_ptr<MiniChunk>> result;
	vector<vmath::ivec3> potential_mini_coords;

	const vmath::ivec3 mini_coords = get_mini_coords(x, y, z);
	const vmath::ivec3 mini_relative_coords = get_mini_relative_coords(x, y, z);

	potential_mini_coords.push_back(mini_coords);

	if (mini_relative_coords[0] == 0) potential_mini_coords.push_back(mini_coords + IWEST);
	if (mini_relative_coords[0] == 15) potential_mini_coords.push_back(mini_coords + IEAST);

	if (mini_relative_coords[1] == 0 && y > 0) potential_mini_coords.push_back(mini_coords + IDOWN * MINICHUNK_HEIGHT);
	if (mini_relative_coords[1] == 15 && y + MINICHUNK_HEIGHT < 256) potential_mini_coords.push_back(mini_coords + IUP * MINICHUNK_HEIGHT);
	if (mini_relative_coords[2] == 0) potential_mini_coords.push_back(mini_coords + INORTH);
	if (mini_relative_coords[2] == 15) potential_mini_coords.push_back(mini_coords + ISOUTH);

	for (auto& coords : potential_mini_coords) {
		const auto mini = get_mini(coords);
		if (mini != nullptr) {
			result.push_back(mini);
		}
	}

	return result;
}

// get a block's type
// inefficient when called repeatedly - if you need multiple blocks from one mini/chunk, use get_mini (or get_chunk) and mini.get_block.
BlockType WorldDataPart::get_type(const int x, const int y, const int z) {
	std::shared_ptr<Chunk> chunk = get_chunk_containing_block(x, z);

	if (!chunk) {
		return BlockType::Air;
	}

	const vmath::ivec3 chunk_coords = get_chunk_relative_coordinates(x, y, z);

	return chunk->get_block(chunk_coords);
}

BlockType WorldDataPart::get_type(const vmath::ivec3& xyz) { return get_type(xyz[0], xyz[1], xyz[2]); }
BlockType WorldDataPart::get_type(const vmath::ivec4& xyz_) { return get_type(xyz_[0], xyz_[1], xyz_[2]); }

// set a block's type
// inefficient when called repeatedly
void WorldDataPart::set_type(const int x, const int y, const int z, const BlockType& val) {
	std::shared_ptr<Chunk> chunk = get_chunk_containing_block(x, z);

	if (!chunk) {
		return;
	}

	const vmath::ivec3 chunk_coords = get_chunk_relative_coordinates(x, y, z);
	chunk->set_block(chunk_coords, val);
}

void WorldDataPart::set_type(const vmath::ivec3& xyz, const BlockType& val) { return set_type(xyz[0], xyz[1], xyz[2], val); }
void WorldDataPart::set_type(const vmath::ivec4& xyz_, const BlockType& val) { return set_type(xyz_[0], xyz_[1], xyz_[2], val); }

// when a mini updates, update its and its neighbors' meshes, if required.
// mini: the mini that changed
// block: the mini-coordinates of the block that was added/deleted
// TODO: Use block.
void WorldDataPart::on_mini_update(std::shared_ptr<MiniChunk> mini, const vmath::ivec3& block) {
	// for now, don't care if something was done in an unloaded mini
	if (mini == nullptr) {
		return;
	}

	// regenerate neighbors' meshes
	const auto neighbors = get_minis_touching_block(block[0], block[1], block[2]);
	for (auto& neighbor : neighbors) {
		if (neighbor != mini) {
			enqueue_mesh_gen(neighbor, true);
		}
	}

	// regenerate own meshes
	enqueue_mesh_gen(mini, true);

	// finally, add nearby waters to propagation queue
	// TODO: do this smarter?
	schedule_water_propagation(block);
	schedule_water_propagation_neighbors(block);
}

// update meshes
void WorldDataPart::on_block_update(const vmath::ivec3& block) {
	std::shared_ptr<MiniChunk> mini = get_mini_containing_block(block[0], block[1], block[2]);
	vmath::ivec3 mini_coords = get_mini_relative_coords(block[0], block[1], block[2]);
	on_mini_update(mini, block);
}

void WorldDataPart::destroy_block(const int x, const int y, const int z) {
	// update data
	std::shared_ptr<MiniChunk> mini = get_mini_containing_block(x, y, z);
	const vmath::ivec3 mini_coords = get_mini_relative_coords(x, y, z);
	mini->set_block(mini_coords, BlockType::Air);

	// regenerate textures for all neighboring minis (TODO: This should be a maximum of 3 neighbors, since >=3 sides of the destroyed block are facing its own mini.)
	on_mini_update(mini, { x, y, z });
}

void WorldDataPart::destroy_block(const vmath::ivec3& xyz) { return destroy_block(xyz[0], xyz[1], xyz[2]); };

void WorldDataPart::add_block(const int x, const int y, const int z, const BlockType& block) {
	// update data
	std::shared_ptr<MiniChunk> mini = get_mini_containing_block(x, y, z);
	const vmath::ivec3& mini_coords = get_mini_relative_coords(x, y, z);
	mini->set_block(mini_coords, block);

	// regenerate textures for all neighboring minis (TODO: This should be a maximum of 3 neighbors, since the block always has at least 3 sides inside its mini.)
	on_mini_update(mini, { x, y, z });
}

void WorldDataPart::add_block(const vmath::ivec3& xyz, const BlockType& block) { return add_block(xyz[0], xyz[1], xyz[2], block); };

// TODO
Metadata WorldDataPart::get_metadata(const int x, const int y, const int z) {
	std::shared_ptr<Chunk> chunk = get_chunk_containing_block(x, z);

	if (!chunk) {
		return 0;
	}

	const vmath::ivec3 chunk_coords = get_chunk_relative_coordinates(x, y, z);
	return chunk->get_metadata(chunk_coords);
}

Metadata WorldDataPart::get_metadata(const vmath::ivec3& xyz) { return get_metadata(xyz[0], xyz[1], xyz[2]); }
Metadata WorldDataPart::get_metadata(const vmath::ivec4& xyz_) { return get_metadata(xyz_[0], xyz_[1], xyz_[2]); }

// TODO
void WorldDataPart::set_metadata(const int x, const int y, const int z, const Metadata& val) {
	std::shared_ptr<Chunk> chunk = get_chunk_containing_block(x, z);

	if (!chunk) {
		OutputDebugString("Warning: Set metadata for unloaded chunk.\n");
		return;
	}

	vmath::ivec3 chunk_coords = get_chunk_relative_coordinates(x, y, z);
	chunk->set_metadata(chunk_coords, val);
}

void WorldDataPart::set_metadata(const vmath::ivec3& xyz, const Metadata& val) { return set_metadata(xyz[0], xyz[1], xyz[2], val); }
void WorldDataPart::set_metadata(const vmath::ivec4& xyz_, const Metadata& val) { return set_metadata(xyz_[0], xyz_[1], xyz_[2], val); }

// TODO
void WorldDataPart::schedule_water_propagation(const vmath::ivec3& xyz) {
	// push to water propagation priority queue
	water_propagation_queue.push({ current_tick + 5, xyz });
}

void WorldDataPart::schedule_water_propagation_neighbors(const vmath::ivec3& xyz) {
	auto directions = { INORTH, ISOUTH, IEAST, IWEST, IDOWN };
	for (const auto& ddir : directions) {
		schedule_water_propagation(xyz + ddir);
	}
}

// get liquid at (x, y, z) and propagate it
void WorldDataPart::propagate_water(int x, int y, int z) {
	vmath::ivec3 coords = { x, y, z };

	// get chunk which block is in, and if it's unloaded, don't both propagating
	auto chunk = get_chunk_containing_block(x, z);
	if (chunk == nullptr) {
		return;
	}

	// get block at propagation location
	auto block = get_type(x, y, z);

	// if we're air or flowing water, adjust height
	if (block == BlockType::Air || block == BlockType::FlowingWater) {
#ifdef _DEBUG
		char buf[256];
		sprintf(buf, "Propagating water at (%d, %d, %d)\n", x, y, z);
		OutputDebugString(buf);
#endif // _DEBUG

		uint8_t water_level = block == BlockType::FlowingWater ? get_metadata(x, y, z).get_liquid_level() : block == BlockType::StillWater ? 7 : 0;
		uint8_t new_water_level = water_level;

		// if water on top, max height
		auto top_block = get_type(coords + IUP);
		if (top_block == BlockType::StillWater || top_block == BlockType::FlowingWater) {
			// update water level if needed
			new_water_level = 7; // max
			if (new_water_level != water_level) {
				set_type(x, y, z, BlockType::FlowingWater);
				set_metadata(x, y, z, new_water_level);
				schedule_water_propagation_neighbors(coords);
				on_block_update(coords);
			}
			return;
		}

		/* UPDATE WATER LEVEL FOR CURRENT BLOCK BY CHECKING SIDES */

		// record highest water level in side blocks, out of side blocks that are ON a block
		uint8_t highest_side_water = 0;
		auto directions = { INORTH, ISOUTH, IEAST, IWEST };
		for (auto& ddir : directions) {
			// BEAUTIFUL - don't inherit height from nearby water UNLESS it's ON A SOLID BLOCK!
			BlockType under_side_block = get_type(coords + ddir + IDOWN);
			if (under_side_block.is_nonsolid()) {
				continue;
			}

			BlockType side_block = get_type(coords + ddir);

			// if side block is still, its level is max
			if (side_block == BlockType::StillWater) {
				highest_side_water = 7;
				break;
			}

			// if side block is flowing, update highest side water
			else if (side_block == BlockType::FlowingWater) {
				auto side_water_level = get_metadata(coords + ddir).get_liquid_level();
				if (side_water_level > highest_side_water) {
					highest_side_water = side_water_level;
					if (side_water_level == 7) {
						break;
					}
				}
			}
		}

		/* UPDATE WATER LEVEL FOR CURRENT BLOCK IF IT'S CHANGED */

		// update water level if needed
		new_water_level = highest_side_water - 1;
		if (new_water_level != water_level) {
			// if water level in range, set it
			if (0 <= new_water_level && new_water_level <= 7) {
				set_type(x, y, z, BlockType::FlowingWater);
				set_metadata(x, y, z, new_water_level);
				schedule_water_propagation_neighbors(coords);
				on_block_update(coords);
			}
			// otherwise destroy water
			else if (block == BlockType::FlowingWater) {
				set_type(x, y, z, BlockType::Air);
				schedule_water_propagation_neighbors(coords);
				on_block_update(coords);
			}
		}
	}
}

// given water at (x, y, z), find all directions which lead to A shortest path down
// radius = 4
// TODO
std::unordered_set<vmath::ivec3, vecN_hash> WorldDataPart::find_shortest_water_path(int x, int y, int z) {
	vmath::ivec3 coords = { x, y, z };
	assert(get_type(coords + IDOWN).is_solid() && "block under starter block is non-solid!");

	// extract a 9x9 radius of blocks which we can traverse (1) and goals (2)

	constexpr unsigned radius = 4;
	constexpr unsigned invalid_path = 0;
	constexpr unsigned valid_path = 1;
	constexpr unsigned goal = 2;

	static_assert(radius > 0);
	uint8_t extracted[radius * 2 - 1][radius * 2 - 1];
	memset(extracted, invalid_path, sizeof(extracted));

	// for every block in radius
	for (int dx = -radius; dx <= radius; dx++) {
		for (int dz = -radius; dz <= radius; dz++) {
			// if the block is empty
			if (get_type(x + dx, y, z + dz) == BlockType::Air) {
				// if the block below it is solid, it's a valid path to take
				if (get_type(x + dx, y - 1, z + dz).is_solid()) {
					extracted[x + dx][z + dz] = valid_path;
				}
				// if the block below it is non-solid, it's a goal
				else {
					extracted[x + dx][z + dz] = goal;
				}
			}
		}
	}

	// find all shortest paths (1) to goal (2) using bfs
	struct search_item {
		int distance = -1;
		bool reachable_from_west = false;
		bool reachable_from_east = false;
		bool reachable_from_north = false;
		bool reachable_from_south = false;
	};

	search_item search_items[radius * 2 + 1][radius * 2 + 1];

	std::queue<vmath::ivec2> bfs_queue;

	// set very center
	search_items[radius][radius].distance = 0;

	// set items around center
	search_items[radius + 1][radius].distance = 1;
	search_items[radius + 1][radius].reachable_from_west = true;

	search_items[radius - 1][radius].distance = 1;
	search_items[radius - 1][radius].reachable_from_east = true;

	search_items[radius][radius + 1].distance = 1;
	search_items[radius][radius + 1].reachable_from_north = true;

	search_items[radius][radius - 1].distance = 1;
	search_items[radius][radius - 1].reachable_from_south = true;

	// insert items around center into queue
	// TODO: do clever single for loop instead of creating std::vector?
	std::vector<std::pair<int, int>> nearby = { {1, 0}, {-1, 0}, {0, 1}, {0, -1} };
	for (auto& [dx, dy] : nearby) {
		bfs_queue.push({ dx, dy });
	}

	// store queue items that have shortest distance
	int found_distance = -1;
	std::vector<search_item*> found; // TODO: only store valid directions. Or even better, just have 4 bools, then convert them to coords when returning.

	// perform bfs
	while (!bfs_queue.empty()) {
		// get first item in bfs
		const auto item_coords = bfs_queue.front();
		bfs_queue.pop();
		auto& item = search_items[item_coords[0] + radius][item_coords[1] + radius];
		const auto block_type = extracted[item_coords[0] + radius][item_coords[1] + radius];

		// if invalid location, yeet out
		if (block_type == invalid_path) {
			break;
		}

		// if we've already found, and this is too big, yeet out
		if (found_distance >= 0 && found_distance < item.distance) {
			break;
		}

		// if item is a goal, add it to found
		if (block_type == goal) {
			assert(found_distance == -1 || found_distance == item.distance);
			found_distance = item.distance;
			found.push_back(&item);
		}
		// otherwise, add its neighbors
		else {
			for (auto& [dx, dy] : nearby) {
				// if item_coords + [dx, dy] in range
				if (abs(item_coords[0] + dx) <= radius && abs(item_coords[1] + dy) <= radius) {
					// add it
					bfs_queue.push({ item_coords[0] + dx, item_coords[1] + dy });
				}
			}
		}
	}

	std::unordered_set<vmath::ivec3, vecN_hash> reachable_from_dirs;
	for (auto& result : found) {
		if (result->reachable_from_east) {
			reachable_from_dirs.insert(IEAST);
		}
		if (result->reachable_from_west) {
			reachable_from_dirs.insert(IWEST);
		}
		if (result->reachable_from_north) {
			reachable_from_dirs.insert(INORTH);
		}
		if (result->reachable_from_south) {
			reachable_from_dirs.insert(ISOUTH);
		}
	}

	return reachable_from_dirs;
}

// For a certain corner, get height of flowing water at that corner
float WorldDataPart::get_water_height(const vmath::ivec3& corner) {
#ifdef _DEBUG
	bool any_flowing_water = false;
#endif

	vector<float> water_height_factors;
	water_height_factors.reserve(4);

	for (int i = 0; i < 4; i++) {
		const int dx = (i % 1 == 0) ? 0 : -1; //  0, -1,  0, -1
		const int dz = (i / 2 == 0) ? 0 : -1; //  0,  0, -1, -1

		const vmath::ivec3 block_coords = corner + vmath::ivec3(dx, 0, dz);
		const BlockType block = get_type(block_coords);
		const Metadata metadata = get_metadata(block_coords);

		switch ((BlockType::Value)block) {
		case BlockType::Air:
			water_height_factors.push_back(0);
			break;
		case BlockType::FlowingWater:
#ifdef _DEBUG
			any_flowing_water = true;
#endif
			water_height_factors.push_back(metadata.get_liquid_level());
			break;
		case BlockType::StillWater:
			water_height_factors.push_back(8);
			break;
		default:
			water_height_factors.push_back(7);
			break;
		}
	}

#ifdef _DEBUG
	assert(any_flowing_water && "called get_water_height for a corner without any nearby flowing water!");
#endif

	assert(!water_height_factors.empty());

	return std::accumulate(water_height_factors.begin(), water_height_factors.end(), 0) / water_height_factors.size();
}

void WorldDataPart::handle_messages()
{
	// Receive all messages
	std::vector<zmq::message_t> message;
	auto ret = zmq::recv_multipart(bus.out, std::back_inserter(message), zmq::recv_flags::dontwait);
	while (ret)
	{
		// Get chunk gen response
		if (message[0].to_string_view() == msg::CHUNK_GEN_RESPONSE)
		{
			// Extract result
			ChunkGenResponse* response_ = *message[1].data<ChunkGenResponse*>();
			std::unique_ptr<ChunkGenResponse> response(response_);

			// Get the chunk
			std::shared_ptr<Chunk> chunk = std::move(response->chunk);
			assert(chunk);

			// make sure it's not a duplicate
			if (get_chunk(chunk->coords))
			{
				OutputDebugStringA("Warn: Duplicate chunk generated.\n");
			}
			else
			{
				add_chunk(response->coords[0], response->coords[1], chunk);
			}

			// Now we must enqueue all minis and neighboring minis for meshing
			for (int i = 0; i < MINIS_PER_CHUNK; i++)
			{
				enqueue_mesh_gen(chunk->minis[i]);
			}

			std::shared_ptr<Chunk> c;
#define ENQUEUE(chunk_ivec2)\
			c = get_chunk(chunk_ivec2);\
			if (c)\
			{\
				for (int i = 0; i < MINIS_PER_CHUNK; i++)\
				{\
					enqueue_mesh_gen(c->minis[i]);\
				}\
			}

			ENQUEUE(chunk->coords + vmath::ivec2(1, 0));
			ENQUEUE(chunk->coords + vmath::ivec2(-1, 0));
			ENQUEUE(chunk->coords + vmath::ivec2(0, 1));
			ENQUEUE(chunk->coords + vmath::ivec2(0, -1));
#undef ENQUEUE
		}
		else
		{
#ifndef _DEBUG
			WindowsException("unknown message");
#endif // _DEBUG
		}

		message.clear();
		ret = zmq::recv_multipart(bus.out, std::back_inserter(message), zmq::recv_flags::dontwait);
	}
}

/**
 * ? Get NEGATIVE? height liquid should be draw at, given negative water level.
 * (I.e. fullness level goes from 0 (full) to 7 (almost empty), and we return a similar ratio.)
 * /
constexpr  float liquid_level_to_height(int liquid_level) {
	// if empty (8 (or more)), height is 0 -- WHY? Shouldn't it be 1?
	if (liquid_level >= 8) {
		liquid_level = 0;
	}

	// liquidLevel is in [0,   7  ]
	// result      is in [1/9, 8/9]
	return (liquid_level + 1) / 9.0f;
}

 float get_liquid_height(int x, int y, int z, BlockType block) {
	int sumDivisor = 0;
	float heightSum = 0.0F;

	// for all blocks around the corner
	for (int i = 0; i < 4; ++i) {

		// (newX, y, newZ) is one block surrounding the corner (x, y, z)
		int newX = x - (i & 1); // goes x, x-1, x, x-1
		int newZ = z - (i >> 1 & 1); // goes z, z, z-1, z-1

		// if same liquid on top, set to max height
		if (get_type(newX, y + 1, newZ) == block) {
			return 1.0f;
		}

		// get material at (newX, y, newZ)
		BlockType newBlock = get_type(newX, y, newZ);

		// if same material as the liquid we're deciding height for,
		if (newBlock == block)
		{
			// get liquid level at (newX, y, newZ)
			// NOTE: liquid level 0 = max, 7 = min.
			int liquidLevel = get_metadata(newX, y, newZ).get_liquid_level();

			// ? sanity check + if minimum level
			if (liquidLevel >= 8 || liquidLevel == 0)
			{
				heightSum += liquid_level_to_height(liquidLevel) * 10.0F;
				sumDivisor += 10;
			}

			heightSum += liquid_level_to_height(liquidLevel);
			++sumDivisor;
		}
		// if newMaterial is different than given material and non-solid (e.g. air or a different liquid)
		else if (!newBlock.isSolid())
		{
			// increase sum/divisor, but have a much smaller effect than when same liquid
			++heightSum;
			++sumDivisor;
		}
	}

	return 1.0F - heightSum / static_cast<float>(sumDivisor);
}
*/

World::World(std::shared_ptr<zmq::context_t> ctx_) : data(ctx_), last_update_time(0), bus(ctx_)
{
	// TODO: Move bus out of WorldDataPart
#ifdef _DEBUG
	bus.out.setsockopt(ZMQ_SUBSCRIBE, "", 0);
#else
	for (const auto& m : msg::world_thread_incoming)
	{
		// TODO: Upgrade zmq and replace this with .set()
		bus.out.setsockopt(ZMQ_SUBSCRIBE, m.c_str(), m.size());
	}
#endif // _DEBUG
}

void World::update_world(float time) {
	const auto start_of_fn = std::chrono::high_resolution_clock::now();

	// change in time
	const float dt = time - last_update_time;
	last_update_time = time;
	data.update_tick((int)floorf(time * 20));

	/* CHANGES IN WORLD */
	data.handle_messages();

	// update player movement
	update_player_movement(dt);

	// keep track of if it's in water or not
	const BlockType face_block = data.get_type(vec2ivec(player.coords + vmath::vec4(0, CAMERA_HEIGHT, 0, 0)));
	player.in_water = face_block == BlockType::StillWater || face_block == BlockType::FlowingWater;

	// update last chunk coords
	const auto chunk_coords = get_chunk_coords((int)floorf(player.coords[0]), (int)floorf(player.coords[2]));
	if (chunk_coords != player.chunk_coords) {
		player.chunk_coords = chunk_coords;

		// Notify listeners that last chunk coords have changed
		std::vector<zmq::const_buffer> result({
			zmq::buffer(msg::EVENT_PLAYER_MOVED_CHUNKS),
			zmq::buffer(&player.chunk_coords, sizeof(player.chunk_coords))
			});
		auto ret = zmq::send_multipart(bus.in, result, zmq::send_flags::dontwait);
		assert(ret);

		// Remember to generate nearby chunks
		player.should_check_for_nearby_chunks = true;
	}

	// generate nearby chunks if required
	if (player.should_check_for_nearby_chunks) {
		data.gen_nearby_chunks(player.coords, player.render_distance);
		player.should_check_for_nearby_chunks = false;
	}

	// update block that player is staring at
	const auto direction = player.staring_direction();
	raycast(player.coords + vmath::vec4(0, CAMERA_HEIGHT, 0, 0), direction, 40, &player.staring_at, &player.staring_at_face, [this](const vmath::ivec3& coords, const vmath::ivec3& face) {
		const auto block = this->data.get_type(coords);
		return block.is_solid();
		});

	// make sure rendering didn't take too long
	const auto end_of_fn = std::chrono::high_resolution_clock::now();
	const long result_total = std::chrono::duration_cast<std::chrono::microseconds>(end_of_fn - start_of_fn).count();
#ifdef _DEBUG
	if (result_total / 1000.0f > 50) {
		std::stringstream buf;
		buf << "TOTAL GAME::update_world TIME: " << result_total / 1000.0f << "ms\n";
		OutputDebugString(buf.str().c_str());
	}
#endif // _DEBUG
}

// update player's movement based on how much time has passed since we last did it
void World::update_player_movement(const float dt) {
	/* VELOCITY FALLOFF */

	//   TODO: Handle walking on blocks, in water, etc. Maybe do it based on friction.
	//   TODO: Tweak values.
	player.velocity *= static_cast<float>(pow(0.5, dt));
	vmath::vec4 norm = normalize(player.velocity);
	for (int i = 0; i < 4; i++) {
		if (player.velocity[i] > 0.0f) {
			player.velocity[i] = static_cast<float>(fmaxf(0.0f, player.velocity[i] - (10.0f * norm[i] * dt)));
		}
		else if (player.velocity[i] < 0.0f) {
			player.velocity[i] = static_cast<float>(fmin(0.0f, player.velocity[i] - (10.0f * norm[i] * dt)));
		}
	}

	/* ACCELERATION */

	// character's horizontal rotation
	vmath::mat4 dir_rotation = rotate_pitch_yaw(0.0f, player.yaw);

	// calculate acceleration
	vmath::vec4 acceleration = { 0.0f };

	if (player.actions.forwards) {
		acceleration += dir_rotation * vmath::vec4(0.0f, 0.0f, -1.0f, 0.0f);
	}
	if (player.actions.backwards) {
		acceleration += dir_rotation * vmath::vec4(0.0f, 0.0f, 1.0f, 0.0f);
	}
	if (player.actions.left) {
		acceleration += dir_rotation * vmath::vec4(-1.0f, 0.0f, 0.0f, 0.0f);
	}
	if (player.actions.right) {
		acceleration += dir_rotation * vmath::vec4(1.0f, 0.0f, 0.0f, 0.0f);
	}
	if (player.actions.jumping) {
		acceleration += vmath::vec4(0.0f, 1.0f, 0.0f, 0.0f);
	}
	if (player.actions.shifting) {
		acceleration += dir_rotation * vmath::vec4(0.0f, -1.0f, 0.0f, 0.0f);
	}

	/* VELOCITY INCREASE */

	player.velocity += acceleration * dt * 50.0f;
	if (length(player.velocity) > 10.0f) {
		player.velocity = 10.0f * normalize(player.velocity);
	}
	player.velocity[3] = 0.0f; // Just in case

	/* POSITION CHANGE */

	// Calculate our change-in-position
	vmath::vec4 position_change = player.velocity * dt;

	// Adjust it to avoid collisions
	vmath::vec4 fixed_position_change = position_change;
	if (!player.noclip) {
		fixed_position_change = prevent_collisions(position_change);
	}

	/* SNAP TO WALLS */

	vmath::ivec4 ipos = vec2ivec(player.coords);

	// if removed east, snap to east wall
	if (position_change[0] > fixed_position_change[0]) {
		player.velocity[0] = 0;
		player.coords[0] = fmin(player.coords[0], ipos[0] + 1.0f - PLAYER_RADIUS); // RESET EAST
	}
	// west
	if (position_change[0] < fixed_position_change[0]) {
		player.velocity[0] = 0;
		player.coords[0] = fmaxf(player.coords[0], ipos[0] + PLAYER_RADIUS); // RESET WEST
	}
	// north
	if (position_change[2] < fixed_position_change[2]) {
		player.velocity[2] = 0;
		player.coords[2] = fmaxf(player.coords[2], ipos[2] + PLAYER_RADIUS); // RESET NORTH
	}
	// south
	if (position_change[2] > fixed_position_change[2]) {
		player.velocity[2] = 0;
		player.coords[2] = fmin(player.coords[2], ipos[2] + 1.0f - PLAYER_RADIUS); // RESET SOUTH
	}
	// up
	if (position_change[1] > fixed_position_change[1]) {
		player.velocity[1] = 0;
		player.coords[1] = fmin(player.coords[1], ipos[1] + 2.0f - PLAYER_HEIGHT); // RESET UP
	}
	// down
	if (position_change[1] < fixed_position_change[1]) {
		player.velocity[1] = 0;
		player.coords[1] = fmaxf(player.coords[1], static_cast<float>(ipos[1])); // RESET DOWN
	}

	// Update position
	player.coords += fixed_position_change;
}

// given a player's change-in-position, modify the change to optimally prevent collisions
vmath::vec4 World::prevent_collisions(const vmath::vec4& position_change) {
	// TODO: prioritize removing velocity that won't change our position when snapping.

	// Get all blocks we might be intersecting with
	auto blocks = get_player_intersecting_blocks(player.coords + position_change);

	// if all blocks are non-solid, we done
	if (all_of(begin(blocks), end(blocks), [this](const auto& block_coords) { auto block = data.get_type(block_coords); return block.is_nonsolid(); })) {
		return position_change;
	}

	// indices of position-change array
	vector<int> indices = argsort(3, &position_change[0]);

	// TODO: Instead of removing 1 or 2 separately, group them together, and remove the ones with smallest length.
	// E.g. if velocity is (2, 2, 10), and have to either remove (2,2) or (10), remove (2,2) because sqrt(2^2+2^2) = sqrt(8) < 10.

	assert(indices[0] + indices[1] + indices[2] == 3);

	// try removing just one velocity
	for (int i = 0; i < 3; i++) {
		vmath::vec4 position_change_fixed = position_change;
		position_change_fixed[indices[i]] = 0.0f;
		blocks = get_player_intersecting_blocks(player.coords + position_change_fixed);

		// if all blocks are non-solid, we done
		if (all_of(begin(blocks), end(blocks), [this](const auto& block_coords) { auto block = data.get_type(block_coords); return block.is_nonsolid(); })) {
			return position_change_fixed;
		}
	}

	// indices for pairs of velocities
	vmath::ivec2 pair_indices[3] = {
		{0, 1},
		{0, 2},
		{1, 2},
	};

	// sort again, this time based on 2d-vector length
	std::sort(std::begin(pair_indices), std::end(pair_indices), [position_change](const auto pair1, const auto pair2) {
		return length(vmath::vec2(position_change[pair1[0]], position_change[pair1[1]])) < vmath::length(vmath::vec2(position_change[pair2[0]], position_change[pair2[1]]));
		});

	// try removing two velocities
	for (int i = 0; i < 3; i++) {
		vmath::vec4 position_change_fixed = position_change;
		position_change_fixed[pair_indices[i][0]] = 0.0f;
		position_change_fixed[pair_indices[i][1]] = 0.0f;
		blocks = get_player_intersecting_blocks(player.coords + position_change_fixed);

		// if all blocks are air, we done
		if (all_of(begin(blocks), end(blocks), [this](const auto& block_coords) { auto block = data.get_type(block_coords); return block.is_nonsolid(); })) {
			return position_change_fixed;
		}
	}

	// after all this we still can't fix it? Frick, just don't move player then.
	OutputDebugString("Holy fuck it's literally unfixable.\n");
	return { 0 };
}
