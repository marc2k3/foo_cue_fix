#define _WIN32_WINNT _WIN32_WINNT_WIN7
#define WINVER _WIN32_WINNT_WIN7

#include <algorithm>
#include <string>
#include <concurrent_unordered_set.h>
#include <foobar2000/SDK/foobar2000.h>

#pragma comment(lib, "shlwapi.lib")

namespace
{
	static constexpr const char* component_name = "Cue Fix";

	DECLARE_COMPONENT_VERSION(
		component_name,
		"1.0.6",
		"Copyright (C) 2022 marc2003\n\n"
		"Build: " __TIME__ ", " __DATE__
	);

	VALIDATE_COMPONENT_FILENAME("foo_cue_fix.dll");

	class CueFix : public threaded_process_callback
	{
	public:
		CueFix(size_t playlist, metadb_handle_list_cref handles) : m_playlist(playlist), m_handles(handles), m_count(handles.get_count())
		{
			m_remove.resize(m_count);
		}

		void on_done(ctx_t, bool)
		{
			if (m_remove_count > 0)
			{
				auto plman = playlist_manager::get();
				plman->playlist_remove_items(m_playlist, m_remove);

				pfc::string8 playlist_name;
				plman->playlist_get_name(m_playlist, playlist_name);
				FB2K_console_formatter() << component_name << ": found " << m_remove_count << " item(s) to remove on playlist named " << playlist_name;
			}
		}

		void run(threaded_process_status&, abort_callback&) final
		{
			pfc::bit_array_bittable ignore(m_count);
			pfc::array_t<std::string> paths;
			paths.set_size(m_count);

			metadb_v2::get()->queryMultiParallel_(m_handles, [&](size_t idx, const metadb_v2::rec_t& rec)
				{
					const std::string path = m_handles[idx]->get_path();
					paths[idx] = path;

					if (!path.starts_with("file://"))
					{
						ignore.set(idx, true);
						return;
					}

					if (rec.info.is_empty()) return;
					
					const char* filename = rec.info->info().info_get("referenced_file");
					if (filename == nullptr) return;

					ignore.set(idx, true);
					const pfc::string8 parent_folder = pfc::string_directory(path.c_str());
					const pfc::string8 referenced_file = pfc::io::path::combine(parent_folder, filename);

					if (is_file(referenced_file))
					{
						m_referenced_files.insert(referenced_file.get_ptr());
					}
					else
					{
						m_remove.set(idx, true);
						m_remove_count++;
					}
				});

			if (m_referenced_files.size() > 0)
			{
				for (size_t i = 0; i < m_count; ++i)
				{
					if (ignore.get(i) || m_remove.get(i)) continue;
					auto& path = paths[i];

					const auto it = std::ranges::find_if(m_referenced_files, [path](auto&& referenced_file) -> bool
						{
							return stricmp_utf8(path.c_str(), referenced_file.c_str()) == 0;
						});

					if (it != m_referenced_files.end())
					{
						m_remove.set(i, true);
						m_remove_count++;
					}
				}
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

		concurrency::concurrent_unordered_set<std::string> m_referenced_files;
		metadb_handle_list m_handles;
		pfc::bit_array_bittable m_remove;
		size_t m_count{}, m_playlist{};
		std::atomic_size_t m_remove_count{};
	};

	class PlaylistCallbackStatic : public playlist_callback_static
	{
	public:
		uint32_t get_flags() final
		{
			return flag_on_items_added;
		}

		void on_items_added(size_t playlist, size_t, metadb_handle_list_cref, const pfc::bit_array&) final
		{
			auto plman = playlist_manager::get();

			if (plman->playlist_lock_get_filter_mask(playlist) & playlist_lock::filter_remove)
			{
				//FB2K_console_formatter() << "doing nothing, playlist lock prevents removal of items";
				return;
			}

			metadb_handle_list items;
			plman->playlist_get_all_items(playlist, items);

			auto cb = fb2k::service_new<CueFix>(playlist, items);
			threaded_process::get()->run_modeless(cb, threaded_process::flag_silent, core_api::get_main_window(), component_name);
		}

		void on_default_format_changed() final {}
		void on_items_modified(size_t, const pfc::bit_array&) final {}
		void on_items_modified_fromplayback(size_t, const pfc::bit_array&, playback_control::t_display_level) final {}
		void on_items_removing(size_t, const pfc::bit_array&, size_t, size_t) final {}
		void on_items_replaced(size_t, const pfc::bit_array&, const pfc::list_base_const_t<t_on_items_replaced_entry>&) final {}
		void on_playlists_removing(const pfc::bit_array&, size_t, size_t) final {}
		void on_item_ensure_visible(size_t, size_t) final {}
		void on_item_focus_change(size_t, size_t, size_t) final {}
		void on_items_removed(size_t, const pfc::bit_array&, size_t, size_t) final {}
		void on_items_reordered(size_t, const size_t*, size_t) final {}
		void on_items_selection_change(size_t, const pfc::bit_array&, const pfc::bit_array&) final {}
		void on_playback_order_changed(size_t) final {}
		void on_playlist_activate(size_t, size_t) final {}
		void on_playlist_created(size_t, const char*, size_t) final {}
		void on_playlist_locked(size_t, bool) final {}
		void on_playlist_renamed(size_t, const char*, size_t) final {}
		void on_playlists_removed(const pfc::bit_array&, size_t, size_t) final {}
		void on_playlists_reorder(const size_t*, size_t) final {}
	};

	FB2K_SERVICE_FACTORY(PlaylistCallbackStatic);
};
