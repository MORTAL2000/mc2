#version 450 core

in flat uint gs_block_type;
in vec2 gs_tex_coords; // texture coords in [0.0, 1.0]
in flat ivec3 gs_face;


layout (std140, binding = 0) uniform UNI_IN
{
	// member			base alignment			base offset		size	aligned offset	extra info
	mat4 mv_matrix;		// 16 (same as vec4)	0				64		16 				(mv_matrix[0])
						//						16						32				(mv_matrix[1])
						//						32						48				(mv_matrix[2])
						//						48						64				(mv_matrix[3])
	mat4 proj_matrix;	// 16 (same as vec4)	64				64		80				(proj_matrix[0])
						//						80						96				(proj_matrix[1])
						//						96						112				(proj_matrix[2])
						//						112						128				(proj_matrix[3])
	uint in_water;      // 4                    128             4       132
} uni;

out vec4 color;

// block textures
layout (binding = 0) uniform sampler2DArray block_textures[3];
// 0: top_textures
// 1: side_textures
// 2: bottom_textures

float soft_increase(float x) {
	return -1/((x+1)) + 1;
}

float soft_increase2(float x, float cap) {
	return min(sqrt(x/cap), 1.0);
}

void main(void)
{
	color = texture(block_textures[1 - sign(gs_face[1])],  vec3(gs_tex_coords, gs_block_type));
	
	// discard stuff that we can barely see, else all it's gonna do is mess with our depth buffer
	if (color.a == 0) {
		discard;
	}

	// if in water, make everything blue depending on depth
	if (bool(uni.in_water) && gs_block_type != 9) {
		// get depth of current fragment
		float depth = gl_FragCoord.z / gl_FragCoord.w;
		
		float strength = soft_increase(depth/5.0f);

		// tint fragment blue
		vec4 ocean_blue = vec4(0.0, 0.0, 0.5, 1.0);
		color = mix(color, ocean_blue, strength);
	}
}
