#include "world.h"

/*************************************************************/
/* PLACING TESTS IN HERE UNTIL I LEARN HOW TO DO IT PROPERLY */
/*************************************************************/

namespace WorldTests {

	void run_all_tests() {
		test_gen_quads();
		test_mark_as_merged();
		test_get_max_size();
		test_gen_layer();
		OutputDebugString("WorldTests completed successfully.\n");
	}

	void test_gen_layer() {
		// fuck this is too hard
	}

	void test_gen_quads() {
		// create layer of all air
		Block layer[16][16];
		memset(layer, (uint8_t)Block::Air, 16 * 16 * sizeof(Block));

		// 1. Add rectangle from (1,3) to (3,6)
		for (int i = 1; i <= 3; i++) {
			for (int j = 3; j <= 6; j++) {
				layer[i][j] = Block::Stone;
			}
		}
		// expected result
		Quad2D q1;
		q1.block = Block::Stone;
		q1.corners[0] = { 1, 3 };
		q1.corners[1] = { 4, 7 };

		// 2. Add plus symbol - line from (7,5)->(7,9), and (5,7)->(9,7)
		for (int i = 7; i <= 7; i++) {
			for (int j = 5; j <= 9; j++) {
				layer[i][j] = Block::Stone;
			}
		}
		for (int i = 5; i <= 9; i++) {
			for (int j = 7; j <= 7; j++) {
				layer[i][j] = Block::Stone;
			}
		}
		// expected result
		vector<Quad2D> vq2;
		Quad2D q2;
		q2.block = Block::Stone;

		q2.corners[0] = { 5, 7 };
		q2.corners[1] = { 10, 8 };
		vq2.push_back(q2);

		q2.corners[0] = { 7, 5 };
		q2.corners[1] = { 8, 7 };
		vq2.push_back(q2);

		q2.corners[0] = { 7, 8 };
		q2.corners[1] = { 8, 10 };
		vq2.push_back(q2);

		// Finally, add line all along bottom
		for (int i = 0; i <= 15; i++) {
			for (int j = 15; j <= 15; j++) {
				layer[i][j] = Block::Grass;
			}
		}
		// expected result
		Quad2D q3;
		q3.block = Block::Grass;
		q3.corners[0] = { 0, 15 };
		q3.corners[1] = { 16, 16 };

		vector<Quad2D> result = World::gen_quads(layer);

		assert(result.size() == 5 && "wrong number of results");
		assert(find(begin(result), end(result), q1) != end(result) && "q1 not in results list");
		for (auto q : vq2) {
			assert(find(begin(result), end(result), q) != end(result) && "q2's element not in results list");
		}
		assert(find(begin(result), end(result), q3) != end(result) && "q3 not in results list");
	}

	void test_mark_as_merged() {
		bool merged[16][16];
		memset(merged, 0, 16 * 16 * sizeof(bool));

		ivec2 start = { 3, 4 };
		ivec2 max_size = { 2, 5 };

		World::mark_as_merged(merged, start, max_size);

		for (int i = 0; i < 16; i++) {
			for (int j = 0; j < 16; j++) {
				// if in right x-range and y-range
				if (start[0] <= i && i <= start[0] + max_size[0] - 1 && start[1] <= j && j <= start[1] + max_size[1] - 1) {
					// make sure merged
					if (!merged[i][j]) {
						throw "not merged when should be!";
					}
				}
				// else make sure not merged
				else {
					if (merged[i][j]) {
						throw "merged when shouldn't be!";
					}
				}
			}
		}
	}

	void test_get_max_size() {
		// create layer of all air
		Block layer[16][16];
		memset(layer, (uint8_t)Block::Air, 16 * 16 * sizeof(Block));

		// let's say nothing is merged yet
		bool merged[16][16];
		memset(merged, false, 16 * 16 * sizeof(bool));

		// 1. Add rectangle from (1,3) to (3,6)
		for (int i = 1; i <= 3; i++) {
			for (int j = 3; j <= 6; j++) {
				layer[i][j] = Block::Stone;
			}
		}

		// 2. Add plus symbol - line from (7,5)->(7,9), and (5,7)->(9,7)
		for (int i = 7; i <= 7; i++) {
			for (int j = 5; j <= 9; j++) {
				layer[i][j] = Block::Stone;
			}
		}
		for (int i = 5; i <= 9; i++) {
			for (int j = 7; j <= 7; j++) {
				layer[i][j] = Block::Stone;
			}
		}

		// 3. Add line all along bottom
		for (int i = 0; i <= 15; i++) {
			for (int j = 15; j <= 15; j++) {
				layer[i][j] = Block::Grass;
			}
		}

		// Get max size for rectangle top-left-corner
		ivec2 max_size1 = World::get_max_size(layer, merged, { 1, 3 }, Block::Stone);
		if (max_size1[0] != 3 || max_size1[1] != 4) {
			throw "wrong max_size1";
		}

		// Get max size for plus center
		ivec2 max_size2 = World::get_max_size(layer, merged, { 7, 7 }, Block::Stone);
		if (max_size2[0] != 3 || max_size2[1] != 1) {
			throw "wrong max_size2";
		}

	}

	// given a layer and start point, find its best dimensions
	inline ivec2 get_max_size(Block layer[16][16], ivec2 start_point, Block block_type) {
		assert(block_type != Block::Air);

		// TODO: Start max size at {1,1}, and for loops at +1.
		// TODO: Search width with find() instead of a for loop.

		// "max width and height"
		ivec2 max_size = { 0, 0 };

		// maximize width
		for (int i = start_point[0], j = start_point[1]; i < 16; i++) {
			// if extended by 1, add 1 to max width
			if (layer[i][j] == block_type) {
				max_size[0]++;
			}
			// else give up
			else {
				break;
			}
		}

		assert(max_size[0] > 0 && "WTF? Max width is 0? Doesn't make sense.");

		// now that we've maximized width, need to
		// maximize height

		// for each height
		for (int j = start_point[1]; j < 16; j++) {
			// check if entire width is correct
			for (int i = start_point[0]; i < start_point[0] + max_size[0]; i++) {
				// if wrong block type, give up on extending height
				if (layer[i][j] != block_type) {
					break;
				}
			}

			// yep, entire width is correct! Extend max height and keep going
			max_size[1]++;
		}

		assert(max_size[1] > 0 && "WTF? Max height is 0? Doesn't make sense.");
	}

}

MiniChunkMesh* World::gen_minichunk_mesh(MiniChunk* mini) {
	// got our mesh
	MiniChunkMesh* mesh = new MiniChunkMesh();

	// for all 6 sides
	for (int i = 0; i < 6; i++) {
		bool backface = i < 3;
		int layers_idx = i % 3;

		// working indices are always gonna be xy, xz, or yz.
		int working_idx_1, working_idx_2;
		gen_working_indices(layers_idx, working_idx_1, working_idx_2);

		// generate face variable
		ivec3 face = { 0, 0, 0 };
		// I don't think it matters whether we start with front or back face, as long as we switch halfway through.
		// BACKFACE => +X/+Y/+Z SIDE. 
		face[layers_idx] = backface ? -1 : 1;

		// for each layer
		for (int i = 0; i < 16; i++) {
			Block layer[16][16];

			// extract it from the data
			gen_layer(mini, layers_idx, i, face, layer);

			// get quads from layer
			vector<Quad2D> quads2d = gen_quads(layer);

			// if -x, -y, or +z, flip triangles around so that we're not drawing them backwards
			if (face[0] < 0 || face[1] < 0 || face[2] > 0) {
				for (auto &quad2d : quads2d) {
					ivec2 diffs = quad2d.corners[1] - quad2d.corners[0];
					quad2d.corners[0][0] += diffs[0];
					quad2d.corners[1][0] -= diffs[0];
				}
			}

			// convert quads back to 3D coordinates
			vector<Quad3D> quads = quads_2d_3d(quads2d, layers_idx, i, face);

			// if -x, -y, or -z, move 1 forwards
			if (face[0] > 0 || face[1] > 0 || face[2] > 0) {
				for (auto &quad : quads) {
					quad.corners[0] += face;
					quad.corners[1] += face;
				}
			}

			// append quads
			for (auto quad : quads) {
				mesh->quads3d.push_back(quad);
			}
		}
	}

	return mesh;
}
