#define _WIN32_WINNT _WIN32_WINNT_WIN7
#define WINVER _WIN32_WINNT_WIN7
#define NOMINMAX

#include <set>
#include <foobar2000/SDK/foobar2000.h>

#pragma comment(lib, "shlwapi.lib")

namespace
{
	static constexpr const char* component_name = "Cue Fix";

	DECLARE_COMPONENT_VERSION(
		component_name,
		"1.0.2",
		"Copyright (C) 2022 marc2003\n\n"
		"Build: " __TIME__ ", " __DATE__
	);

	VALIDATE_COMPONENT_FILENAME("foo_cue_fix.dll");

	class CueFix : public threaded_process_callback
	{
	public:
		CueFix(size_t playlist, metadb_handle_list_cref handles) : m_playlist(playlist), m_handles(handles) {}

		void run(threaded_process_status&, abort_callback&) override
		{
			const size_t count = m_handles.get_count();
			size_t to_remove_count{};

			pfc::bit_array_bittable is_cue(count);
			pfc::bit_array_bittable to_remove(count);
			std::set<pfc::string8> referenced_files;
			std::vector<pfc::string8> paths;

			auto recs = metadb_v2::get()->queryMultiSimple(m_handles);

			for (size_t i = 0; i < count; ++i)
			{
				const pfc::string8 path = m_handles[i]->get_path();
				paths.emplace_back(path);

				if (recs[i].info.is_empty()) continue;
				const char* filename = recs[i].info->info().info_get("referenced_file");
				if (filename == nullptr) continue;

				is_cue.set(i, true);

				const pfc::string8 parent_folder = pfc::string_directory(path);
				const pfc::string8 referenced_file = pfc::io::path::combine(parent_folder, filename);

				if (is_file(referenced_file))
				{
					referenced_files.emplace(referenced_file);
				}
				else
				{
					to_remove.set(i, true);
					to_remove_count++;
				}
			}

			if (referenced_files.size() > 0)
			{
				for (size_t i = 0; i < count; ++i)
				{
					if (to_remove.get(i)) continue;
					if (is_cue.get(i)) continue;

					const auto it = std::ranges::find_if(referenced_files, [path = paths[i]](const pfc::string8& referenced_file) -> bool
						{
							return _stricmp(path, referenced_file) == 0;
						});

					if (it != referenced_files.end())
					{
						to_remove.set(i, true);
						to_remove_count++;
					}
				}
			}

			if (to_remove_count > 0)
			{
				fb2k::inMainThread([=, p = m_playlist]
					{
						auto api = playlist_manager::get();
						api->playlist_remove_items(p, to_remove);

						pfc::string8 playlist_name;
						api->playlist_get_name(p, playlist_name);
						FB2K_console_formatter() << component_name << ": found " << to_remove_count << " item(s) to remove on playlist named " << playlist_name;
					});
			}
		}

	private:
		bool is_file(const char* path)
		{
			try
			{
				return filesystem::g_exists(path, fb2k::noAbort);
			}
			catch (...) {}
			return false;
		}

		metadb_handle_list m_handles;
		size_t m_playlist{};
	};

	class PlaylistCallbackStatic : public playlist_callback_static
	{
	public:
		uint32_t get_flags() override
		{
			return flag_on_items_added;
		}

		void on_items_added(size_t playlist, size_t, metadb_handle_list_cref, const pfc::bit_array&) override
		{
			auto api = playlist_manager::get();

			if (api->playlist_lock_get_filter_mask(playlist) & playlist_lock::filter_remove)
			{
				//FB2K_console_formatter() << "doing nothing, playlist lock prevents removal of items";
				return;
			}

			metadb_handle_list items;
			api->playlist_get_all_items(playlist, items);

			auto cb = fb2k::service_new<CueFix>(playlist, items);
			threaded_process::get()->run_modeless(cb, threaded_process::flag_silent, core_api::get_main_window(), component_name);
		}

		void on_default_format_changed() override {}
		void on_items_modified(size_t, const pfc::bit_array&) override {}
		void on_items_modified_fromplayback(size_t, const pfc::bit_array&, playback_control::t_display_level) override {}
		void on_items_removing(size_t, const pfc::bit_array&, size_t, size_t) override {}
		void on_items_replaced(size_t, const pfc::bit_array&, const pfc::list_base_const_t<t_on_items_replaced_entry>&) override {}
		void on_playlists_removing(const pfc::bit_array&, size_t, size_t) override {}
		void on_item_ensure_visible(size_t, size_t) override {}
		void on_item_focus_change(size_t, size_t, size_t) override {}
		void on_items_removed(size_t, const pfc::bit_array&, size_t, size_t) override {}
		void on_items_reordered(size_t, const size_t*, size_t) override {}
		void on_items_selection_change(size_t, const pfc::bit_array&, const pfc::bit_array&) override {}
		void on_playback_order_changed(size_t) override {}
		void on_playlist_activate(size_t, size_t) override {}
		void on_playlist_created(size_t, const char*, size_t) override {}
		void on_playlist_locked(size_t, bool) override {}
		void on_playlist_renamed(size_t, const char*, size_t) override {}
		void on_playlists_removed(const pfc::bit_array&, size_t, size_t) override {}
		void on_playlists_reorder(const size_t*, size_t) override {}
	};

	FB2K_SERVICE_FACTORY(PlaylistCallbackStatic);
};
