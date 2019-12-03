#ifndef __MINICHUNK_H__
#define __MINICHUNK_H__

#include "block.h"
#include "chunkdata.h"
#include "minichunkmesh.h"
#include "render.h"
#include "util.h"

#include <algorithm> // std::find

#define MINICHUNK_WIDTH 16
#define MINICHUNK_HEIGHT 16
#define MINICHUNK_DEPTH 16
#define MINICHUNK_SIZE (MINICHUNK_WIDTH * MINICHUNK_DEPTH * MINICHUNK_HEIGHT)

class MiniChunk : public ChunkData {
public:
	vmath::ivec3 coords; // coordinates in minichunk format (chunk base x / 16, chunk base y, chunk base z / 16) (NOTE: y NOT DIVIDED BY 16 (YET?))
	GLuint block_types_buf; // each mini gets its own buf -- easy this way for now
	GLuint quads_buf; // each mini gets its own buf -- easy this way for now
	bool invisible = false;
	MiniChunkMesh* mesh;
	GLuint quad_block_type_buf, quad_corner_buf;

	MiniChunk() : ChunkData(MINICHUNK_WIDTH, MINICHUNK_HEIGHT, MINICHUNK_DEPTH) {

	}

	// render this minichunk!
	void render(OpenGLInfo* glInfo) {
		// don't draw if covered in all sides
		if (invisible) {
			return;
		}

		// cube VAO
		glBindVertexArray(glInfo->vao_cube);

		// bind to chunk-types attribute binding point
		glVertexArrayVertexBuffer(glInfo->vao_cube, glInfo->chunk_types_bidx, block_types_buf, 0, sizeof(Block));

		// write this chunk's coordinate to coordinates buffer
		glNamedBufferSubData(glInfo->trans_buf, TRANSFORM_BUFFER_COORDS_OFFSET, sizeof(ivec3), coords); // Add base chunk coordinates to transformation data

		// draw!
		glDrawArraysInstanced(GL_TRIANGLES, 0, 36, MINICHUNK_SIZE);

		// unbind VAO jic
		glBindVertexArray(0);
	}

	// render this minichunk using mesh strategy
	void render_meshes(OpenGLInfo* glInfo) {
		// don't draw if covered in all sides
		if (invisible) {
			return;
		}

		// quad VAO
		glBindVertexArray(glInfo->vao_quad);

		// bind to quads attribute binding point
		glVertexArrayVertexBuffer(glInfo->vao_quad, glInfo->quad_buf_bidx, quads_buf, 0, sizeof(Quad));

		// write this chunk's coordinate to coordinates buffer
		glNamedBufferSubData(glInfo->trans_buf, TRANSFORM_BUFFER_COORDS_OFFSET, sizeof(ivec3), coords); // Add base chunk coordinates to transformation data

		Quad* quads = (Quad*)malloc((MINICHUNK_SIZE / 8) * sizeof(Quad) * 6);
		glGetNamedBufferSubData(quads_buf, 0, (MINICHUNK_SIZE / 8) * sizeof(Quad) * 6, quads);

		// draw!
		//glDrawArraysInstanced(GL_TRIANGLES, 0, 6, 1);
		char buf[256];
		sprintf(buf, "Drawing quad from (%d, %d, %d) to (%d, %d, %d)\n", quads[0].corners[0][0], quads[0].corners[0][1], quads[0].corners[0][2], quads[0].corners[2][0], quads[0].corners[2][1], quads[0].corners[2][2]);
		OutputDebugString(buf);
		glDrawArrays(GL_TRIANGLES, 0, 6);

		// unbind VAO jic
		glBindVertexArray(0);

		free(quads);
	}

	void render_meshes_simple(OpenGLInfo* glInfo) {
		// don't draw if covered in all sides
		if (invisible) {
			return;
		}

		// DEBUG: only render the one at position 0,0,0!
		if (coords[0] || coords[1] || coords[2]) {
			//return;
		}

		// DEBUG: only render the one at position 0,64,0!
		if (coords[0] || coords[1] != 64 || coords[2]) {
			return;
		}

		char buf[256];
		sprintf(buf, "Minichunk at (%d, %d, %d) is drawing %d quads.\n", coords[0] * 16, coords[1], coords[2] * 16, mesh->quads.size());
		OutputDebugString(buf);

		int not_air = 0;
		for (int i = 0; i < MINICHUNK_SIZE; i++) {
			if (data[i] != Block::Air) {
				not_air++;
			}
		}
		sprintf(buf, "Num not air: %d\n", not_air);
		OutputDebugString(buf);


		// quad VAO
		glBindVertexArray(glInfo->vao_quad);

		// bind to quads attribute binding point
		glVertexArrayVertexBuffer(glInfo->vao_quad, glInfo->quad_block_type_bidx, quad_block_type_buf, 0, sizeof(Block));
		glVertexArrayVertexBuffer(glInfo->vao_quad, glInfo->quad_corner_bidx, quad_corner_buf, 0, sizeof(ivec3));

		// write this chunk's coordinate to coordinates buffer
		glNamedBufferSubData(glInfo->trans_buf, TRANSFORM_BUFFER_COORDS_OFFSET, sizeof(ivec3), coords); // Add base chunk coordinates to transformation data

		// draw!
		//glDrawArraysInstanced(GL_TRIANGLES, 0, 6, mesh->quads.size());
		//glDrawArraysInstanced(GL_TRIANGLES, 0, 6, 1);
		//glDrawArraysInstanced(GL_TRIANGLES, 0, 3, 1);
		//glDrawArraysInstanced(GL_TRIANGLES, 0, 12, 1);
		//glDrawArraysInstanced(GL_TRIANGLES, 0, 6*96, 1);

		for (int i = 0; i < mesh->quads.size(); i++) {
			glDrawArraysInstancedBaseInstance(GL_TRIANGLES, i * 6, 6, 1, i);
		}

		Block* blocks = (Block*)malloc(sizeof(Block) * mesh->quads.size());

		for (int i = 0; i < mesh->quads.size(); i++) {
			// update blocks
			blocks[i] = (Block)mesh->quads[i].block;
		}

		for (int i = 0; i < mesh->quads.size(); i++) {
			Block b = blocks[i];
			if (b == Block::Grass) {
				OutputDebugString("");
			}
		}

		free(blocks);
		//glGetBufferSubData


		// unbind VAO jic
		glBindVertexArray(0);
	}

	// prepare buf for drawing -- only need to call it when new, or when stuff (or nearby stuff) changes
	void prepare_buf() {
		// TODO
	}

	// get MiniChunk's base coords in real coordinates
	inline vmath::ivec3 real_coords() {
		return { coords[0] * 16, coords[1], coords[2] * 16 };
	}

	//// update quads buf with our quad mesh
	//void update_quads_buf() {
	//	if (mesh == nullptr) {
	//		throw "bad";
	//	}

	//	glNamedBufferSubData(quads_buf, 0, sizeof(Quad) * mesh->quads.size(), &mesh->quads[0]);
	//}

	// update quads buf with our quad mesh
	void update_quads_buf() {
		if (mesh == nullptr) {
			throw "bad";
		}

		ivec3* corners = (ivec3*)malloc(sizeof(ivec3) * mesh->quads.size() * 6);
		Block* blocks = (Block*)malloc(sizeof(Block) * mesh->quads.size());

		for (int i = 0; i < mesh->quads.size(); i++) {
			// update blocks
			blocks[i] = (Block)mesh->quads[i].block;

			// update corners
			for (int j = 0; j < 6; j++) {
				switch (j) {
				case 0:
					corners[i * 6 + j] = mesh->quads[i].corners[0];
					break;
				case 1:
				case 4:
					corners[i * 6 + j] = mesh->quads[i].corners[3];
					break;
				case 2:
				case 3:
					corners[i * 6 + j] = mesh->quads[i].corners[1];
					break;
				case 5:
					corners[i * 6 + j] = mesh->quads[i].corners[2];
					break;
				}
			}
		}

		glNamedBufferSubData(quad_block_type_buf, 0, sizeof(Block) * mesh->quads.size(), blocks);
		glNamedBufferSubData(quad_corner_buf, 0, sizeof(ivec3) * mesh->quads.size() * 6, corners);
	}
};

#endif // __MINICHUNK_H__
