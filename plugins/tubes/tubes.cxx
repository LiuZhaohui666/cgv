
// C++ STL
#include <vector>
#include <set>

// CGV framework core
#include <cgv/base/node.h>
#include <cgv/base/register.h>
#include <cgv/gui/provider.h>
#include <cgv/render/drawable.h>
#include <cgv/utils/advanced_scan.h>

// CGV OpenGL lib
#include <cgv_gl/rounded_cone_renderer.h>
#include <cgv_gl/spline_tube_renderer.h>

// fltk_gl_view for controlling instant redraw
#include <plugins/cg_fltk/fltk_gl_view.h>

// stereo_view_interactor for controlling fix_view_up_dir
#include <plugins/crg_stereo_view/stereo_view_interactor.h>

// local includes
#include "traj_loader.h"


////
// Plugin definition

/// baseline visualization plugin for arbitrary trajectory data as tubes using the framework tube renderers and
/// trajectory loading facilities
class tubes :
	public cgv::base::node,             // derive from node to integrate into global tree structure and to store a name
	public cgv::base::argument_handler, // derive from argument handler to be able to process custom arguments
	public cgv::gui::provider,          // derive from provider to obtain a GUI tab
	public cgv::gui::event_handler,     // derive from event handler to be able to directly react to user interaction
	public cgv::render::drawable        // derive from drawable for being able to render
{
public:

	/// real number type
	typedef float real;

	/// renderer type
	enum Renderer { ROUNDED_CONE=0, SPLINE_TUBE=1 };


protected:

	/// path of the dataset to load - can be either a directory or a single file
	std::string datapath;

	/// rendering-related configurable fields
	struct
	{
		// renderer to use
		Renderer renderer = SPLINE_TUBE;

		/// render style for the rounded cone renderer
		cgv::render::surface_render_style render_style;
	} render_cfg;	

	/// misc configurable fields
	struct
	{
		/// proxy for controlling fltk_gl_view::instant_redraw
		bool instant_redraw_proxy = false;

		/// proxy for controlling stereo_view_interactor::fix_view_up_dir
		bool fix_view_up_dir_proxy = false;
	} misc_cfg;

	/// rendering state fields
	struct
	{
		/// style for the rounded cone renderer
		cgv::render::rounded_cone_render_style rounded_cone_rstyle;

		/// style for the spline tube renderer
		cgv::render::spline_tube_render_style spline_tube_rstyle;

		/// shared attribute array manager used by both renderers
		cgv::render::attribute_array_manager aam;

		/// accessor for the render data generated by the trajectory manager
		const traj_manager<float>::render_data *data;
	} render;

	/// drag-n-drop state fields
	struct
	{
		/// current mouse position
		ivec2 pos;

		/// current drag-n-drop string
		std::string text;

		/// list of filenames extracted from @ref #text
		std::vector<std::string> filenames;
	} dnd;

	/// dataset state fields
	struct
	{
		/// set of filepaths for loading
		std::set<std::string> files;
	} dataset;

	/// trajectory manager
	traj_manager<float> traj_mgr;


public:
	tubes() : node("tubes_instance")
	{
		// adjusted rounded cone renderer defaults
		render.rounded_cone_rstyle.enable_ambient_occlusion = true;
	}

	void handle_args (std::vector<std::string> &args)
	{
		// look out for potential dataset files/dirs
		std::vector<unsigned> arg_ids;
		for (unsigned i=0; i<(unsigned)args.size(); i++)
			if (traj_mgr.can_load(args[i]))
			{
				// this appears to be a dataset file we're supposed to load
				dataset.files.emplace(args[i]);
				arg_ids.emplace_back(i);
			}

		// process our arguments (if any)
		if (!arg_ids.empty())
		{
			// remove the arguments we grabbed
			for (signed i=(signed)arg_ids.size()-1; i>=0; i--)
				args.erase(args.begin() + arg_ids[i]);
			// announce change in dataset_fn
			on_set(&dataset);
		}
	}

	std::string get_type_name (void) const
	{
		return "tubes";
	}

	bool self_reflect (cgv::reflect::reflection_handler &rh)
	{
		return
			rh.reflect_member("datapath", datapath) &&
			rh.reflect_member("renderer", render_cfg.renderer) &&
			rh.reflect_member("render_style", render_cfg.render_style) &&
			rh.reflect_member("instant_redraw_proxy", misc_cfg.instant_redraw_proxy) &&
			rh.reflect_member("fix_view_up_dir_proxy", misc_cfg.fix_view_up_dir_proxy);
	}

	bool init (cgv::render::context &ctx)
	{
		// increase reference count of both renderers by one
		auto &rcr = cgv::render::ref_rounded_cone_renderer(ctx, 1);
		auto &str = cgv::render::ref_spline_tube_renderer(ctx, 1);
		bool success = rcr.ref_prog().is_linked() && str.ref_prog().is_linked();

		// init shared attribute array manager
		success = success && render.aam.init(ctx);

		// done
		return success;
	}

	void clear (cgv::render::context &ctx)
	{
		// decrease reference count of both renderers by one
		cgv::render::ref_spline_tube_renderer(ctx, -1);
		cgv::render::ref_rounded_cone_renderer(ctx, -1);
	}

	void stream_help (std::ostream &os)
	{
		os << "tubes" << std::endl;
	}

	bool handle (cgv::gui::event &e)
	{
		if (e.get_kind() == cgv::gui::EID_MOUSE)
		{
			cgv::gui::mouse_event &me = static_cast<cgv::gui::mouse_event&>(e);
			// select drag and drop events only
			if ((me.get_flags() & cgv::gui::EF_DND) != 0) switch (me.get_action())
			{
				case cgv::gui::MA_ENTER:
				{
					// store (and process) dnd text on enter event, since it's not available in drag events
					dnd.text = me.get_dnd_text();
					std::vector<cgv::utils::line> lines;
					cgv::utils::split_to_lines(dnd.text, lines, true);
					dnd.filenames.reserve(lines.size());
					for (const auto &line : lines)
						dnd.filenames.emplace_back(cgv::utils::to_string(line));
					return true;
				}

				case cgv::gui::MA_DRAG:
				{
					// during dragging keep track of drop position and redraw
					dnd.pos = ivec2(me.get_x(), me.get_y());
					post_redraw();
					return true;
				}

				case cgv::gui::MA_LEAVE:
				{
					// when mouse leaves window, cancel the dnd operation (and redraw to clear the dnd indicator
					// onscreen text)
					dnd.text.clear();
					dnd.filenames.clear();
					post_redraw();
					return true;
				}

				case cgv::gui::MA_RELEASE:
				{
					// process the files that where dropped onto the window
					dataset.files.clear();
					for (const std::string &filename : dnd.filenames)
						dataset.files.emplace(filename);
					dnd.filenames.clear();
					dnd.text.clear();
					on_set(&dataset);
					return true;
				}

				default:
					/* DoNothing() */;
			}
		}
		return false;
	}

	void init_frame (cgv::render::context &ctx)
	{
		if (misc_cfg.fix_view_up_dir_proxy)
			// ToDo: make stereo view interactors reflect this property
			/*dynamic_cast<stereo_view_interactor*>(find_view_as_node())->set(
				"fix_view_up_dir", misc_cfg.fix_view_up_dir_proxy
			);*/
			find_view_as_node()->set_view_up_dir(0, 1, 0);
	}

	void draw (cgv::render::context &ctx)
	{
		// display drag-n-drop information, if a dnd operation is in progress
		if (!dnd.text.empty())
		{
			static const rgb dnd_col(1, 0.5f, 0.5f);
			// compile the text we're going to draw and gather its approximate dimensions at the same time
			float w = 0, s = ctx.get_current_font_size();
			std::stringstream dnd_drawtext;
			dnd_drawtext << "Load dataset:" << std::endl;
			{
				std::string tmp;
				for (const std::string &filename : dnd.filenames)
				{
					tmp = "   "; tmp += filename;
					w = std::max(w, ctx.get_current_font_face()->measure_text_width(tmp, s));
					dnd_drawtext << tmp << std::endl;
				}
			}
			float h = dnd.filenames.size()*s + s;
			// gather our available screen estate
			GLint vp[4]; glGetIntegerv(GL_VIEWPORT, vp);
			// calculate actual position at which to place the text
			ivec2 pos = dnd.pos,
			      overflow(vp[0]+vp[2] - dnd.pos.x()-int(std::ceil(w)),
			               vp[1]+vp[3] - dnd.pos.y()-int(std::ceil(h)));
			// - first, try to prevent truncation at the right and bottom borders
			if (overflow.x() < 0)
				pos.x() = std::max(1, pos.x()+overflow.x());
			if (overflow.y() < 0)
				pos.y() = std::max(1, pos.y()+overflow.y());
			// - then, absolutely prevent truncation at the top border
			pos.y() = std::max(vp[1]+signed(s), pos.y());
			// draw the text
			ctx.push_pixel_coords();
				ctx.set_color(dnd_col);
				ctx.set_cursor(vecn(float(pos.x()), float(pos.y())), "", cgv::render::TA_TOP_LEFT);
				ctx.output_stream() << dnd_drawtext.str();
				ctx.output_stream().flush();
			ctx.pop_pixel_coords();
		}

		// draw dataset using selected renderer
		if (traj_mgr.has_data())
		{
			switch (render_cfg.renderer)
			{
				case ROUNDED_CONE:
				{
					auto &rcr = cgv::render::ref_rounded_cone_renderer(ctx);
					rcr.set_render_style(render.rounded_cone_rstyle);
					rcr.set_attribute_array_manager(ctx, &render.aam);
					rcr.render(ctx, 0, render.data->indices.size());
					break;
				}

				case SPLINE_TUBE:
				{
					auto &str = cgv::render::ref_spline_tube_renderer(ctx);
					str.set_render_style(render.spline_tube_rstyle);
					str.set_attribute_array_manager(ctx, &render.aam);
					str.render(ctx, 0, render.data->indices.size());
				}
			}
		}
	}

	void create_gui (void)
	{
		// dataset settings
		add_decorator("Dataset", "heading", "level=1");
		add_member_control(this, "data file/path", datapath);

		// rendering settings
		add_decorator("Rendering", "heading", "level=1");
		add_member_control(
			this, "renderer", render_cfg.renderer, "dropdown",
			"enums='ROUNDED_CONE=0,SPLINE_TUBE=1';tooltip='The built-in renderer to use for drawing the tubes.'"
		);
		if (begin_tree_node("tube surface material", render_cfg, false))
		{
			align("\a");
			add_gui("render_style", render_cfg.render_style);
			align("\b");
			end_tree_node(render_cfg);
		}

		// Misc settings contractable section
		add_decorator("Miscellaneous", "heading", "level=1");
		if (begin_tree_node("tools (persistent by default)", misc_cfg, false))
		{
			align("\a");
			add_member_control(
				this, "instant_redraw_proxy", misc_cfg.instant_redraw_proxy, "toggle",
				"tooltip='Controls the instant redraw state of the FLTK GL window.'"
			);
			add_member_control(
				this, "fix_view_up_dir_proxy", misc_cfg.fix_view_up_dir_proxy, "toggle",
				"tooltip='Controls the \"fix_view_up_dir\" state of the view interactor.'"
			);
			align("\b");
			end_tree_node(misc_cfg);
		}
	}

	void update_attribute_bindings (void)
	{
		auto &ctx = *get_context();
		if (traj_mgr.has_data()) switch (render_cfg.renderer)
		{
			case ROUNDED_CONE:
			{
				auto &rcr = cgv::render::ref_rounded_cone_renderer(ctx);
				rcr.set_attribute_array_manager(ctx, &render.aam);
				rcr.set_position_array(ctx, render.data->positions);
				rcr.set_radius_array(ctx, render.data->radii);
				rcr.set_color_array(ctx, render.data->colors);
				rcr.set_indices(ctx, render.data->indices);
				return;
			}
			case SPLINE_TUBE:
			{
				auto &str = cgv::render::ref_spline_tube_renderer(ctx);
				str.set_attribute_array_manager(ctx, &render.aam);
				str.set_position_array(ctx, render.data->positions);
				str.set_tangent_array(ctx, render.data->tangents);
				str.set_radius_array(ctx, render.data->radii);
				str.set_color_array(ctx, render.data->colors);
				str.set_indices(ctx, render.data->indices);
			}
		}
	}

	void on_set (void *member_ptr)
	{
		// dataset settings
		// - configurable datapath
		if (member_ptr == &datapath && !datapath.empty())
		{
			traj_mgr.clear();
			if (traj_mgr.load(datapath))
			{
				dataset.files.clear();
				dataset.files.emplace(datapath);
				render.data = &(traj_mgr.get_render_data());
				update_attribute_bindings();
			}
		}
		// - non-configurable dataset logic
		else if (member_ptr == &dataset)
		{
			// clear current dataset
			datapath.clear();
			traj_mgr.clear();

			// load new data
			bool loaded_something = false;
			for (const auto &file : dataset.files)
				loaded_something = traj_mgr.load(file) || loaded_something;
			update_member(&datapath);

			// update render state
			if (loaded_something)
			{
				render.data = &(traj_mgr.get_render_data());
				update_attribute_bindings();
			}
		}

		// render settings
		if (member_ptr == &render_cfg.renderer)
			// re-route the attribute bindings in the shared attrib. array manager for the newly selected renderer
			update_attribute_bindings();

		// misc settings
		// - instant redraw
		else if (member_ptr == &misc_cfg.instant_redraw_proxy)
			// ToDo: handle the (virtually impossible) case that some other plugin than cg_fltk provides the gl_context
			dynamic_cast<fltk_gl_view*>(get_context())->set_void("instant_redraw", "bool", member_ptr);
		// - fix view up dir
		else if (member_ptr == &misc_cfg.fix_view_up_dir_proxy)
			// ToDo: make stereo view interactors reflect this property, and handle the case that some other plugin that
			//       is not derived from stereo_view_interactor handles viewing
			//if (!misc_cfg.fix_view_up_dir_proxy)
			//	dynamic_cast<stereo_view_interactor*>(find_view_as_node())->set("fix_view_up_dir", false);
			//else
			if (misc_cfg.fix_view_up_dir_proxy)
				find_view_as_node()->set_view_up_dir(0, 1, 0);

		// default implementation for all members
		// - dirty hack to catch GUI changes to render_cfg.render_style
		*dynamic_cast<cgv::render::surface_render_style*>(&render.rounded_cone_rstyle) = render_cfg.render_style;
		*dynamic_cast<cgv::render::surface_render_style*>(&render.spline_tube_rstyle) = render_cfg.render_style;
		// - remaining logic
		update_member(member_ptr);
		post_redraw();
	}
};


////
// Object registration

cgv::base::object_registration<tubes> reg_tubes("");

#ifdef CGV_FORCE_STATIC
	cgv::base::registration_order_definition ro_def("stereo_view_interactor;tubes");
#endif
