#include "color_map_editor.h"

#include <cgv/defines/quote.h>
#include <cgv/gui/dialog.h>
#include <cgv/gui/key_event.h>
#include <cgv/gui/mouse_event.h>
#include <cgv/gui/theme_info.h>
#include <cgv/math/ftransform.h>
#include <cgv/utils/advanced_scan.h>
#include <cgv/utils/file.h>
#include <cgv_gl/gl/gl.h>

namespace cgv {
namespace glutil {

color_map_editor::color_map_editor() {

	set_name("Color Scale Editor");

	layout.padding = 13; // 10px plus 3px border
	layout.total_height = 60;

	set_overlay_alignment(AO_START, AO_START);
	set_overlay_stretch(SO_HORIZONTAL);
	set_overlay_margin(ivec2(-3));
	set_overlay_size(ivec2(600u, layout.total_height));
	
	fbc.add_attachment("color", "flt32[R,G,B,A]");
	fbc.set_size(get_overlay_size());

	canvas.register_shader("rectangle", canvas::shaders_2d::rectangle);
	canvas.register_shader("circle", canvas::shaders_2d::circle);
	canvas.register_shader("histogram", "hist2d.glpr");
	canvas.register_shader("background", canvas::shaders_2d::background);

	overlay_canvas.register_shader("rectangle", canvas::shaders_2d::rectangle);

	resolution = (cgv::type::DummyEnum)256;

	mouse_is_on_overlay = false;
	show_cursor = false;
	cursor_pos = ivec2(-100);
	cursor_drawtext = "";

	cmc.points.set_drag_callback(std::bind(&color_map_editor::handle_drag, this));
	cmc.points.set_drag_end_callback(std::bind(&color_map_editor::handle_drag_end, this));
	cmc.points.set_constraint(layout.handles_rect);

	handle_renderer = generic_renderer(canvas::shaders_2d::arrow);
}

void color_map_editor::clear(cgv::render::context& ctx) {

	canvas.destruct(ctx);
	overlay_canvas.destruct(ctx);
	fbc.clear(ctx);
}

bool color_map_editor::handle_event(cgv::gui::event& e) {

	// return true if the event gets handled and stopped here or false if you want to pass it to the next plugin
	unsigned et = e.get_kind();
	unsigned char modifiers = e.get_modifiers();

	if (!show)
		return false;

	if (et == cgv::gui::EID_KEY) {
		cgv::gui::key_event& ke = (cgv::gui::key_event&)e;

		if (ke.get_action() == cgv::gui::KA_PRESS) {
			switch (ke.get_key()) {
			case cgv::gui::KEY_Left_Ctrl:
				show_cursor = true;
				cursor_drawtext = "+";
				post_redraw();
				break;
			case cgv::gui::KEY_Left_Alt:
				show_cursor = true;
				cursor_drawtext = "-";
				post_redraw();
				break;
			}
		}
		else if (ke.get_action() == cgv::gui::KA_RELEASE) {
			switch (ke.get_key()) {
			case cgv::gui::KEY_Left_Ctrl:
				show_cursor = false;
				post_redraw();
				break;
			case cgv::gui::KEY_Left_Alt:
				show_cursor = false;
				post_redraw();
				break;
			}
		}
	}
	else if (et == cgv::gui::EID_MOUSE) {
		cgv::gui::mouse_event& me = (cgv::gui::mouse_event&)e;
		cgv::gui::MouseAction ma = me.get_action();

		switch (ma) {
		case cgv::gui::MA_ENTER:
			mouse_is_on_overlay = true;
			break;
		case cgv::gui::MA_LEAVE:
			mouse_is_on_overlay = false;
			post_redraw();
			break;
		case cgv::gui::MA_MOVE:
		case cgv::gui::MA_DRAG:
			cursor_pos = ivec2(me.get_x(), me.get_y());
			if (show_cursor)
				post_redraw();
			break;
		}

		if (me.get_button_state() & cgv::gui::MB_LEFT_BUTTON) {
			if (ma == cgv::gui::MA_PRESS && modifiers > 0) {
				ivec2 mpos = get_local_mouse_pos(ivec2(me.get_x(), me.get_y()));

				switch (modifiers) {
				case cgv::gui::EM_CTRL:
					if (!get_hit_point(mpos))
						add_point(mpos);
					break;
				case cgv::gui::EM_ALT:
				{
					point* hit_point = get_hit_point(mpos);
					if (hit_point)
						remove_point(hit_point);
				}
				break;
				}
			}
		}

		return cmc.points.handle(e, last_viewport_size, container);
	}
	return false;
}

void color_map_editor::on_set(void* member_ptr) {

	if(member_ptr == &layout.total_height) {
		ivec2 size = get_overlay_size();
		size.y() = layout.total_height;
		set_overlay_size(size);
	}

	if(member_ptr == &resolution) {
		context* ctx_ptr = get_context();
		if(ctx_ptr)
			init_texture(*ctx_ptr);
		update_color_map(false);
	}

	for(unsigned i = 0; i < cmc.points.size(); ++i) {
		if(member_ptr == &cmc.points[i].col) {
			update_color_map(true);
			break;
		}
	}
	
	update_member(member_ptr);
	post_redraw();
}

bool color_map_editor::init(cgv::render::context& ctx) {
	
	// get a bold font face to use for the cursor
	auto font = cgv::media::font::find_font("Arial");
	if(!font.empty()) {
		cursor_font_face = font->get_font_face(cgv::media::font::FFA_BOLD);
	}

	bool success = true;

	success &= fbc.ensure(ctx);
	success &= canvas.init(ctx);
	success &= overlay_canvas.init(ctx);
	success &= handle_renderer.init(ctx);

	if(success)
		init_styles(ctx);
	
	init_texture(ctx);
	update_color_map(false);

	rgb a(0.75f);
	rgb b(0.9f);
	std::vector<rgb> bg_data = { a, b, b, a };
	
	bg_tex.destruct(ctx);
	cgv::data::data_view bg_dv = cgv::data::data_view(new cgv::data::data_format(2, 2, TI_FLT32, cgv::data::CF_RGB), bg_data.data());
	bg_tex = texture("flt32[R,G,B]", TF_NEAREST, TF_NEAREST, TW_CLAMP_TO_EDGE, TW_CLAMP_TO_EDGE);
	success &= bg_tex.create(ctx, bg_dv, 0);

	return success;
}

void color_map_editor::init_frame(cgv::render::context& ctx) {

	if(ensure_overlay_layout(ctx)) {
		ivec2 container_size = get_overlay_size();
		layout.update(container_size);

		fbc.set_size(container_size);
		fbc.ensure(ctx);

		canvas.set_resolution(ctx, container_size);
		overlay_canvas.set_resolution(ctx, get_viewport_size());

		auto& bg_prog = canvas.enable_shader(ctx, "background");
		float width_factor = static_cast<float>(layout.color_map_rect.size().x()) / static_cast<float>(layout.color_map_rect.size().y());
		bg_style.texcoord_scaling = vec2(5.0f * width_factor, 5.0f);
		bg_style.apply(ctx, bg_prog);
		canvas.disable_current_shader(ctx);

		update_point_positions();
		sort_points();
		update_geometry();
		cmc.points.set_constraint(layout.handles_rect);
	}

	// TODO: move functionality of testing for theme changes to overlay? (or use observer pattern for theme info)
	auto& ti = cgv::gui::theme_info::instance();
	int theme_idx = ti.get_theme_idx();
	if(last_theme_idx != theme_idx) {
		last_theme_idx = theme_idx;
		init_styles(ctx);
		handle_color = rgba(ti.text(), 1.0f);
		highlight_color = rgba(ti.highlight(), 1.0f);
		highlight_color_hex = ti.highlight_hex();
		update_geometry();
	}
}

void color_map_editor::draw(cgv::render::context& ctx) {

	if(!show)
		return;

	fbc.enable(ctx);
	
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	
	ivec2 container_size = get_overlay_size();
	
	// draw container
	auto& rect_prog = canvas.enable_shader(ctx, "rectangle");
	container_style.apply(ctx, rect_prog);
	canvas.draw_shape(ctx, ivec2(0), container_size);
	
	// draw inner border
	border_style.apply(ctx, rect_prog);
	canvas.draw_shape(ctx, ivec2(layout.padding - 1) + ivec2(0, 10), container_size - 2*layout.padding + 2 - ivec2(0, 10));
	
	if(cmc.cm) {
		// draw color scale texture
		color_map_style.apply(ctx, rect_prog);
		preview_tex.enable(ctx, 0);
		canvas.draw_shape(ctx, layout.color_map_rect.pos(), layout.color_map_rect.size());
		preview_tex.disable(ctx);
		canvas.disable_current_shader(ctx);
	} else {
		canvas.disable_current_shader(ctx);
		// draw editor checkerboard background
		auto& bg_prog = canvas.enable_shader(ctx, "background");
		bg_prog.set_uniform(ctx, "scale_exponent", 1.0f);
		bg_tex.enable(ctx, 0);
		//canvas.draw_shape(ctx, layout.handles_rect.pos(), layout.handles_rect.size());
		canvas.draw_shape(ctx, layout.color_map_rect.pos(), layout.color_map_rect.size());
		bg_tex.disable(ctx);
		canvas.disable_current_shader(ctx);
	}

	// draw separator line
	rect_prog = canvas.enable_shader(ctx, "rectangle");
	border_style.apply(ctx, rect_prog);
	canvas.draw_shape(ctx,
		ivec2(layout.color_map_rect.pos().x(), layout.color_map_rect.box.get_max_pnt().y()),
		ivec2(container_size.x() - 2 * layout.padding, 1)
	);
	canvas.disable_current_shader(ctx);

	// draw control points
	auto& handle_prog = handle_renderer.ref_prog();
	handle_prog.enable(ctx);
	canvas.set_view(ctx, handle_prog);
	handle_prog.disable(ctx);
	handle_renderer.render(ctx, PT_LINES, cmc.handles);

	glDisable(GL_BLEND);

	fbc.disable(ctx);

	// draw frame buffer texture to screen
	auto& final_prog = overlay_canvas.enable_shader(ctx, "rectangle");
	fbc.enable_attachment(ctx, "color", 0);
	overlay_canvas.draw_shape(ctx, get_overlay_position(), container_size);
	fbc.disable_attachment(ctx, "color");

	overlay_canvas.disable_current_shader(ctx);

	// draw cursor decorators to show interaction hints
	if(mouse_is_on_overlay && show_cursor) {
		ivec2 pos = cursor_pos + ivec2(7, 4);

		auto fntf_ptr = ctx.get_current_font_face();
		auto s = ctx.get_current_font_size();

		ctx.enable_font_face(cursor_font_face, s);

		ctx.push_pixel_coords();
		ctx.set_color(rgb(0.0f));
		ctx.set_cursor(vecn(float(pos.x()), float(pos.y())), "", cgv::render::TA_TOP_LEFT);
		ctx.output_stream() << cursor_drawtext;
		ctx.output_stream().flush();
		ctx.pop_pixel_coords();

		ctx.enable_font_face(fntf_ptr, s);
	}

	glEnable(GL_DEPTH_TEST);
}

void color_map_editor::create_gui() {

	create_overlay_gui();

	if(begin_tree_node("Settings", layout, false)) {
		align("\a");
		add_member_control(this, "Height", layout.total_height, "value_slider", "min=40;max=500;step=10;ticks=true");
		add_member_control(this, "Resolution", resolution, "dropdown", "enums='2=2,4=4,8=8,16=16,32=32,64=64,128=128,256=256,512=512,1024=1024,2048=2048'");
		align("\b");
		end_tree_node(layout);
	}

	add_decorator("Control Points", "heading", "level=3");
	auto& points = cmc.points;
	for(unsigned i = 0; i < points.size(); ++i)
		add_member_control(this, "Color " + std::to_string(i), points[i].col, "", &points[i] == cmc.points.get_selected() ? "label_color=" + highlight_color_hex : "");
}

void color_map_editor::create_gui(cgv::gui::provider& p) {

	p.add_member_control(this, "Show", show, "check");
}

bool color_map_editor::was_updated() {
	bool temp = has_updated;
	has_updated = false;
	return temp;
}

void color_map_editor::set_color_map(color_map* cm) {
	cmc.reset();
	cmc.cm = cm;

	if(cmc.cm) {
		auto& cm = *cmc.cm;
		auto& cp = cmc.cm->ref_color_points();
		for(size_t i = 0; i < cp.size(); ++i) {
			point p;
			p.val = cgv::math::clamp(cp[i].first, 0.0f, 1.0f);
			p.col = cp[i].second;
			cmc.points.add(p);
		}
		update_point_positions();
		update_color_map(false);

		post_recreate_gui();
	}
}

void color_map_editor::init_styles(context& ctx) {
	// get theme colors
	auto& ti = cgv::gui::theme_info::instance();
	rgba background_color = rgba(ti.background(), 1.0f);
	rgba group_color = rgba(ti.group(), 1.0f);
	rgba border_color = rgba(ti.border(), 1.0f);

	// configure style for the container rectangle
	container_style.apply_gamma = false;
	container_style.fill_color = group_color;
	container_style.border_color = background_color;
	container_style.border_width = 3.0f;
	container_style.feather_width = 0.0f;
	
	// configure style for the border rectangles
	border_style = container_style;
	border_style.fill_color = border_color;
	border_style.border_width = 0.0f;
	
	// configure style for the color scale rectangle
	color_map_style = border_style;
	color_map_style.use_texture = true;

	// configure style for background
	bg_style.use_texture = true;
	bg_style.apply_gamma = false;
	bg_style.feather_width = 0.0f;
	bg_style.texcoord_scaling = vec2(5.0f, 5.0f);

	auto& bg_prog = canvas.enable_shader(ctx, "background");
	bg_style.apply(ctx, bg_prog);
	canvas.disable_current_shader(ctx);

	// configure style for histogram
	hist_style.use_blending = true;
	hist_style.apply_gamma = false;
	hist_style.feather_width = 0.0f;

	auto& hist_prog = canvas.enable_shader(ctx, "histogram");
	hist_style.apply(ctx, hist_prog);
	canvas.disable_current_shader(ctx);

	// configure style for control handles
	cgv::glutil::arrow2d_style handle_style;
	handle_style.use_blending = true;
	handle_style.apply_gamma = false;
	handle_style.use_fill_color = false;
	handle_style.position_is_center = true;
	handle_style.border_color = rgba(ti.border(), 1.0f);
	handle_style.border_width = 1.5f;
	handle_style.border_radius = 2.0f;
	handle_style.stem_width = 12.0f;
	handle_style.head_width = 12.0f;

	auto& handle_prog = handle_renderer.ref_prog();
	handle_prog.enable(ctx);
	handle_style.apply(ctx, handle_prog);
	handle_prog.disable(ctx);

	// configure style for final blitting of overlay into main frame buffer
	cgv::glutil::shape2d_style final_style;
	final_style.fill_color = rgba(1.0f);
	final_style.use_texture = true;
	final_style.use_blending = false;
	final_style.feather_width = 0.0f;

	auto& final_prog = overlay_canvas.enable_shader(ctx, "rectangle");
	final_style.apply(ctx, final_prog);
	overlay_canvas.disable_current_shader(ctx);
}

void color_map_editor::init_texture(context& ctx) {

	std::vector<uint8_t> data(resolution * 3 * 2, 0u);

	preview_tex.destruct(ctx);
	cgv::data::data_view tf_dv = cgv::data::data_view(new cgv::data::data_format(resolution, 2, TI_UINT8, cgv::data::CF_RGB), data.data());
	preview_tex = texture("uint8[R,G,B]", TF_LINEAR, TF_LINEAR);
	preview_tex.create(ctx, tf_dv, 0);
}

void color_map_editor::add_point(const vec2& pos) {

	if(cmc.cm) {
		point p;
		p.pos = ivec2(pos.x(), layout.handles_rect.pos().y());
		p.update_val(layout, 1.0f);
		p.col = cmc.cm->interpolate_color(p.val);
		cmc.points.add(p);

		update_color_map(true);
	}
}

void color_map_editor::remove_point(const point* ptr) {

	if(cmc.points.size() <= 2)
		return;

	bool removed = false;
	std::vector<point> next_points;
	for(unsigned i = 0; i < cmc.points.size(); ++i) {
		if(&cmc.points[i] != ptr)
			next_points.push_back(cmc.points[i]);
		else
			removed = true;
	}
	cmc.points.ref_draggables() = std::move(next_points);
	
	if(removed)
		update_color_map(true);
}

color_map_editor::point* color_map_editor::get_hit_point(const color_map_editor::vec2& pos) {

	point* hit = nullptr;
	for(unsigned i = 0; i < cmc.points.size(); ++i) {
		point& p = cmc.points[i];
		if(p.is_inside(pos))
			hit = &p;
	}

	return hit;
}

void color_map_editor::handle_drag() {

	cmc.points.get_dragged()->update_val(layout, 1.0f);
	update_color_map(true);
	post_redraw();
}

void color_map_editor::handle_drag_end() {

	update_geometry();
	post_recreate_gui();
	post_redraw();
}

void color_map_editor::sort_points() {

	auto& points = cmc.points;

	if(points.size() > 1) {
		int dragged_point_idx = -1;
		int selected_point_idx = -1;

		const point* dragged_point = points.get_dragged();
		const point* selected_point = points.get_selected();

		std::vector<std::pair<point, int>> sorted(points.size());

		for(unsigned i = 0; i < points.size(); ++i) {
			sorted[i].first = points[i];
			sorted[i].second = i;

			if(dragged_point == &points[i])
				dragged_point_idx = i;
			if(selected_point == &points[i])
				selected_point_idx = i;
		}

		std::sort(sorted.begin(), sorted.end(),
			[](const auto& a, const auto& b) -> bool {
				return a.first.val < b.first.val;
			}
		);

		int new_dragged_point_idx = -1;
		int new_selected_point_idx = -1;

		for(unsigned i = 0; i < sorted.size(); ++i) {
			points[i] = sorted[i].first;
			if(dragged_point_idx == sorted[i].second) {
				new_dragged_point_idx = i;
			}
			if(selected_point_idx == sorted[i].second) {
				new_selected_point_idx = i;
			}
		}

		points.set_dragged(new_dragged_point_idx);
		points.set_selected(new_selected_point_idx);
	}
}

void color_map_editor::update_point_positions() {

	for(unsigned i = 0; i < cmc.points.size(); ++i)
		cmc.points[i].update_pos(layout, 1.0f);
}

void color_map_editor::update_color_map(bool is_data_change) {
	
	context* ctx_ptr = get_context();
	if(!ctx_ptr || !cmc.cm) return;
	context& ctx = *ctx_ptr;

	auto& cm = *cmc.cm;
	//auto& tex = cmc.tex;
	auto& points = cmc.points;
	
	sort_points();

	cm.clear();

	for(unsigned i = 0; i < points.size(); ++i) {
		const point& p = points[i];
		cm.add_color_point(p.val, p.col);
	}

	size_t size = static_cast<size_t>(resolution);
	std::vector<rgb> cs_data = cm.interpolate_color(size);

	std::vector<uint8_t> data(3 * 2 * size);
	for(size_t i = 0; i < size; ++i) {
		rgb col = cs_data[i];
		uint8_t r = static_cast<uint8_t>(255.0f * col.R());
		uint8_t g = static_cast<uint8_t>(255.0f * col.G());
		uint8_t b = static_cast<uint8_t>(255.0f * col.B());

		unsigned idx = 3 * i;
		data[idx + 0] = r;
		data[idx + 1] = g;
		data[idx + 2] = b;
		idx += 3*size;
		data[idx + 0] = r;
		data[idx + 1] = g;
		data[idx + 2] = b;
	}

	preview_tex.destruct(ctx);
	cgv::data::data_view dv = cgv::data::data_view(new cgv::data::data_format(size, 2, TI_UINT8, cgv::data::CF_RGB), data.data());
	preview_tex = texture("uint8[R,G,B]", TF_LINEAR, TF_LINEAR, TW_CLAMP_TO_EDGE, TW_CLAMP_TO_EDGE);
	preview_tex.create(ctx, dv, 0);

	update_geometry();

	has_updated = true;
}

bool color_map_editor::update_geometry() {

	context* ctx_ptr = get_context();
	if(!ctx_ptr || !cmc.cm) return false;
	context& ctx = *ctx_ptr;

	auto& cm = *cmc.cm;
	auto& points = cmc.points;
	auto& handles = cmc.handles;

	handles.clear();
	
	bool success = points.size() > 0;

	for(unsigned i = 0; i < points.size(); ++i) {
		const auto& p = points[i];
		vec2 pos = p.center();
		rgba col = cmc.points.get_selected() == &p ? highlight_color : handle_color;
		handles.add(pos + vec2(0.0f, 2.0f),  col);
		handles.add(pos + vec2(0.0f, 20.0f), col);
	}

	handles.set_out_of_date();

	return success;
}

}
}
