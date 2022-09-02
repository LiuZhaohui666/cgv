#include "radix_sort_4way.h"

namespace cgv {
namespace glutil {

bool radix_sort_4way::load_shader_programs(context& ctx)
{

	bool res = true;
	std::string where = "radix_sort_4way::load_shader_programs()";

	shader_define_map distance_defines;
	shader_code::set_define(distance_defines, "ORDER", sort_order, SO_ASCENDING);
	shader_code::set_define(distance_defines, "INITIALIZE_VALUES", value_init_override, true);
	shader_code::set_define(distance_defines, "USE_AUXILIARY_BUFFER", auxiliary_type_def != "", false);
	shader_code::set_define(distance_defines, "VALUE_TYPE_DEFINITION", value_type_def, std::string("uint"));

	if (data_type_def != "")
		distance_defines["DATA_TYPE_DEFINITION"] = data_type_def;
	if (auxiliary_type_def != "")
		distance_defines["AUXILIARY_TYPE_DEFINITION"] = auxiliary_type_def;
	if (key_definition != "")
		distance_defines["KEY_DEFINITION"] = key_definition;

	/*distance_defines["ORDER"] = std::to_string((int)sort_order);
	distance_defines["INITIALIZE_VALUES"] = value_init_override ? "1" : "0";
	distance_defines["VALUE_TYPE_DEFINITION"] = value_type_def;
	if(data_type_definition != "")
		distance_defines["DATA_TYPE_DEFINITION"] = data_type_definition;
	if(key_definition != "")
		distance_defines["KEY_DEFINITION"] = key_definition;*/

	res = res && shader_library::load(ctx, distance_prog, "distance", distance_defines, true, where);
	res = res && shader_library::load(ctx, scan_local_prog, "scan_local", true, where);
	res = res && shader_library::load(ctx, scan_global_prog, "scan_global", true, where);
	res = res &&
		  shader_library::load(ctx, scatter_prog, "scatter", {{"VALUE_TYPE_DEFINITION", value_type_def}}, true, where);

	return res;
}

bool radix_sort_4way::init(context& ctx, size_t count)
{

	if (!load_shader_programs(ctx))
		return false;

	delete_buffers();

	n = unsigned(count);
	group_size = 64;

	unsigned int block_size = 4 * group_size;

	// Calculate padding for n to next multiple of blocksize.
	n_pad = block_size - (n % (block_size));
	if (n % block_size == 0)
		n_pad = 0;

	num_groups = (n + n_pad + group_size - 1) / group_size;
	num_scan_groups = (n + n_pad + block_size - 1) / block_size;
	unsigned int block_sum_offset_shift = static_cast<unsigned int>(log2f(float(block_size)));

	num_block_sums = num_scan_groups;

	unsigned int num = 1;
	while (num_block_sums > num)
		num <<= 1;
	num_block_sums = num;

	size_t data_size = (n + n_pad) * sizeof(unsigned int);
	size_t blocksums_size = 4 * num_block_sums * sizeof(unsigned int);

	create_buffer(keys_in_ssbo, data_size);
	create_buffer(keys_out_ssbo, data_size);
	create_buffer(values_out_ssbo, value_component_count * data_size);
	create_buffer(prefix_sums_ssbo, data_size / 4);
	create_buffer(block_sums_ssbo, blocksums_size);
	create_buffer(last_sum_ssbo, 4 * sizeof(unsigned int));

	distance_prog.enable(ctx);
	distance_prog.set_uniform(ctx, "n", n);
	distance_prog.set_uniform(ctx, "n_padded", n + n_pad);
	distance_prog.disable(ctx);

	scan_local_prog.enable(ctx);
	scan_local_prog.set_uniform(ctx, "n", n + n_pad);
	scan_local_prog.set_uniform(ctx, "n_scan_groups", num_scan_groups);
	scan_local_prog.set_uniform(ctx, "n_block_sums", num_block_sums);
	scan_local_prog.disable(ctx);

	scan_global_prog.enable(ctx);
	scan_global_prog.set_uniform(ctx, "n_block_sums", num_block_sums);
	scan_global_prog.disable(ctx);

	scatter_prog.enable(ctx);
	scatter_prog.set_uniform(ctx, "n", n + n_pad);
	scatter_prog.set_uniform(ctx, "n_block_sums", num_block_sums);
	scatter_prog.set_uniform(ctx, "last_block_sum_idx", ((n + n_pad) >> block_sum_offset_shift) - 1);
	scatter_prog.disable(ctx);

	return true;
}

double radix_sort_4way::sort(context& ctx, GLuint data_buffer, GLuint value_buffer, const vec3& eye_pos,
							 const vec3& view_dir, GLuint auxiliary_buffer)
{

	GLuint values_in_buffer = value_buffer;

	begin_time_query();

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, data_buffer);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, keys_in_ssbo);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, values_in_buffer);
	if (auxiliary_type_def != "" && auxiliary_buffer > 0)
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, auxiliary_buffer);

	distance_prog.enable(ctx);
	distance_prog.set_uniform(ctx, "eye_pos", eye_pos);
	distance_prog.set_uniform(ctx, "view_dir", view_dir);
	glDispatchCompute(num_scan_groups, 1, 1);
	glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
	distance_prog.disable(ctx);

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, prefix_sums_ssbo);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, block_sums_ssbo);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, last_sum_ssbo);

	for (unsigned int b = 0; b < 32; b += 2) {
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, keys_in_ssbo);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, keys_out_ssbo);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, values_in_buffer);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, values_out_ssbo);

		scan_local_prog.enable(ctx);
		glDispatchCompute(num_scan_groups, 1, 1);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
		scan_local_prog.disable(ctx);

		scan_global_prog.enable(ctx);
		glDispatchCompute(4, 1, 1);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
		scan_global_prog.disable(ctx);

		scatter_prog.enable(ctx);
		glDispatchCompute(num_scan_groups, 1, 1);
		glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
		scatter_prog.disable(ctx);

		std::swap(keys_in_ssbo, keys_out_ssbo);
		std::swap(values_in_buffer, values_out_ssbo);
	}

	double time = end_time_query();

	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, 0);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, 0);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, 0);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, 0);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, 0);

	return -1.0;
}

} // namespace glutil
} // namespace cgv
