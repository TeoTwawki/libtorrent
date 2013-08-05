/*

Copyright (c) 2003, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#ifndef TORRENT_STORAGE_HPP_INCLUDE
#define TORRENT_STORAGE_HPP_INCLUDE

#include <vector>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/function/function2.hpp>
#include <boost/function/function0.hpp>
#include <boost/limits.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/intrusive_ptr.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif


#include "libtorrent/torrent_info.hpp"
#include "libtorrent/piece_picker.hpp"
#include "libtorrent/intrusive_ptr_base.hpp"
#include "libtorrent/peer_request.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/file.hpp"
#include "libtorrent/disk_buffer_holder.hpp"
#include "libtorrent/thread.hpp"
#include "libtorrent/storage_defs.hpp"
#include "libtorrent/allocator.hpp"

// #error remove this once file checking is async
#include "libtorrent/block_cache.hpp"

namespace libtorrent
{
	class session;
	struct file_pool;
	struct disk_io_job;
	struct disk_buffer_pool;
	struct cache_status;
	struct aiocb_pool;
	struct session_settings;

	TORRENT_EXPORT std::vector<std::pair<size_type, std::time_t> > get_filesizes(
		file_storage const& t
		, std::string const& p);

	TORRENT_EXPORT bool match_filesizes(
		file_storage const& t
		, std::string const& p
		, std::vector<std::pair<size_type, std::time_t> > const& sizes
		, bool compact_mode
		, std::string* error = 0);

	TORRENT_EXPORT int bufs_size(file::iovec_t const* bufs, int num_bufs);

	struct TORRENT_EXPORT file_allocation_failed: std::exception
	{
		file_allocation_failed(const char* error_msg): m_msg(error_msg) {}
		virtual const char* what() const throw() { return m_msg.c_str(); }
		virtual ~file_allocation_failed() throw() {}
		std::string m_msg;
	};

	struct TORRENT_EXPORT storage_interface
	{
		storage_interface(): m_disk_pool(0), m_aiocb_pool(0), m_settings(0) {}
		// create directories and set file sizes
		// if allocate_files is true. 
		// allocate_files is true if allocation mode
		// is set to full and sparse files are supported
		// false return value indicates an error
		virtual void initialize(bool allocate_files, storage_error& ec) = 0;

		virtual file::aiocb_t* async_readv(file::iovec_t const* bufs, int slot, int offset, int num_bufs
			, boost::function<void(async_handler*)> const& handler) = 0;
		virtual file::aiocb_t* async_writev(file::iovec_t const* bufs, int slot, int offset, int num_bufs
			, boost::function<void(async_handler*)> const& handler) = 0;

		virtual bool has_any_file(storage_error& ec) = 0;
		virtual void hint_read(int slot, int offset, int len) {}

		virtual int readv(file::iovec_t const* bufs, int slot, int offset
			, int num_bufs, storage_error& ec);
		virtual int writev(file::iovec_t const* bufs, int slot, int offset
			, int num_bufs, storage_error& ec);

		// negative return value indicates an error
		virtual int read(char* buf, int slot, int offset, int size, storage_error& ec) = 0;

		// negative return value indicates an error
		virtual int write(const char* buf, int slot, int offset, int size, storage_error& ec) = 0;

		virtual size_type physical_offset(int slot, int offset) = 0;

		// returns the end of the sparse region the slot 'start'
		// resides in i.e. the next slot with content. If start
		// is not in a sparse region, start itself is returned
		virtual int sparse_end(int start) const { return start; }

		// non-zero return value indicates an error
		virtual void move_storage(std::string const& save_path, storage_error& ec) = 0;

		// verify storage dependent fast resume entries
		virtual bool verify_resume_data(lazy_entry const& rd, storage_error& ec) = 0;

		// write storage dependent fast resume entries
		virtual void write_resume_data(entry& rd, storage_error& ec) const = 0;

		// moves (or copies) the content in src_slot to dst_slot
		virtual void move_slot(int src_slot, int dst_slot, storage_error& ec) = 0;

		// swaps the data in slot1 and slot2
		virtual void swap_slots(int slot1, int slot2, storage_error& ec) = 0;

		// swaps the puts the data in slot1 in slot2, the data in slot2
		// in slot3 and the data in slot3 in slot1
		virtual void swap_slots3(int slot1, int slot2, int slot3, storage_error& ec) = 0;

		// this will close all open files that are opened for
		// writing. This is called when a torrent has finished
		// downloading.
		// non-zero return value indicates an error
		virtual void release_files(storage_error& ec) = 0;

		// this will rename the file specified by index.
		virtual void rename_file(int index, std::string const& new_filenamem, storage_error& ec) = 0;

		// this will close all open files and delete them
		// non-zero return value indicates an error
		virtual void delete_files(storage_error& ec) = 0;

		virtual void finalize_file(int file, storage_error& ec) {}

		disk_buffer_pool* disk_pool() { return m_disk_pool; }
		aiocb_pool* aiocbs() { return m_aiocb_pool; }
		session_settings const& settings() const { return *m_settings; }

		virtual ~storage_interface() {}

		// initialized in piece_manager::piece_manager
		disk_buffer_pool* m_disk_pool;
		aiocb_pool* m_aiocb_pool;
		// initialized in disk_io_thread::perform_async_job
		session_settings* m_settings;
	};

	class default_storage : public storage_interface, boost::noncopyable
	{
	public:
		default_storage(file_storage const& fs, file_storage const* mapped, std::string const& path
			, file_pool& fp, std::vector<boost::uint8_t> const& file_prio);
		~default_storage();

		void finalize_file(int file, storage_error& ec);
		bool has_any_file(storage_error& ec);
		void rename_file(int index, std::string const& new_filename, storage_error& ec);
		void release_files(storage_error& ec);
		void delete_files(storage_error& ec);
		void initialize(bool allocate_files, storage_error& ec);
		void move_storage(std::string const& save_path, storage_error& ec);
		int read(char* buf, int slot, int offset, int size, storage_error& ec);
		int write(char const* buf, int slot, int offset, int size, storage_error& ec);
		int sparse_end(int start) const;
		void hint_read(int slot, int offset, int len);
		int readv(file::iovec_t const* bufs, int slot, int offset, int num_bufs, storage_error& ec);
		int writev(file::iovec_t const* buf, int slot, int offset, int num_bufs, storage_error& ec);
		int readv(file::iovec_t const* bufs, int slot, int offset, int num_bufs);
		int writev(file::iovec_t const* buf, int slot, int offset, int num_bufs);
		size_type physical_offset(int slot, int offset);
		void move_slot(int src_slot, int dst_slot, storage_error& ec);
		void swap_slots(int slot1, int slot2, storage_error& ec);
		void swap_slots3(int slot1, int slot2, int slot3, storage_error& ec);
		bool verify_resume_data(lazy_entry const& rd, storage_error& error);
		void write_resume_data(entry& rd, storage_error& ec) const;

		file::aiocb_t* async_readv(file::iovec_t const* bufs, int slot, int offset, int num_bufs
			, boost::function<void(async_handler*)> const& handler);
		file::aiocb_t* async_writev(file::iovec_t const* bufs, int slot, int offset, int num_bufs
			, boost::function<void(async_handler*)> const& handler);

		// this identifies a read or write operation
		// so that default_storage::readwritev() knows what to
		// do when it's actually touching the file
		struct fileop
		{
			// this is the function to be called on the file object, for
			// regular, aligned, operations
			size_type (file::*regular_op)(size_type file_offset
				, file::iovec_t const* bufs, int num_bufs, error_code& ec, int);
			// this is the function to be called on the file object, for
			// unaligned operations
			size_type (default_storage::*unaligned_op)(boost::intrusive_ptr<file> const& f
				, size_type file_offset, file::iovec_t const* bufs, int num_bufs
				, error_code& ec);
			// this is the function to be called on the file object, for
			// async operations
			file::aiocb_t* (file::*async_op)(size_type offset
				, file::iovec_t const* bufs, int num_bufs
				, aiocb_pool&, int);
			// for async operations, this is the handler that will be added
			// to every aiocb_t in the returned chain
			async_handler* handler;
			// for async operations, this is the returned aiocb_t chain
			file::aiocb_t* ret;
			int cache_setting;
			// file open mode (file::read_only, file::write_only etc.)
			int mode;
			int flags;
			// used for error reporting
			char const* operation_name;
		};

		void delete_one_file(std::string const& p, error_code& ec);
		int readwritev(file::iovec_t const* bufs, int slot, int offset
			, int num_bufs, fileop& op, storage_error& ec);

		size_type read_unaligned(boost::intrusive_ptr<file> const& file_handle
			, size_type file_offset, file::iovec_t const* bufs, int num_bufs, error_code& ec);
		size_type write_unaligned(boost::intrusive_ptr<file> const& file_handle
			, size_type file_offset, file::iovec_t const* bufs, int num_bufs, error_code& ec);

		file_storage const& files() const { return m_mapped_files?*m_mapped_files:m_files; }

		boost::scoped_ptr<file_storage> m_mapped_files;
		file_storage const& m_files;

		// helper function to open a file in the file pool with the right mode
		boost::intrusive_ptr<file> open_file(file_storage::iterator fe, int mode
			, error_code& ec) const;

		std::vector<boost::uint8_t> m_file_priority;
		std::string m_save_path;
		// the file pool is typically stored in
		// the session, to make all storage
		// instances use the same pool
		file_pool& m_pool;

		int m_page_size;
		bool m_allocate_files;
	};

	// this storage implementation does not write anything to disk
	// and it pretends to read, and just leaves garbage in the buffers
	// this is useful when simulating many clients on the same machine
	// or when running stress tests and want to take the cost of the
	// disk I/O out of the picture. This cannot be used for any kind
	// of normal bittorrent operation, since it will just send garbage
	// to peers and throw away all the data it downloads. It would end
	// up being banned immediately
	class disabled_storage : public storage_interface, boost::noncopyable
	{
	public:
		disabled_storage(int piece_size) : m_piece_size(piece_size) {}
		bool has_any_file(storage_error& ec) { return false; }
		void rename_file(int index, std::string const& new_filename, storage_error& ec) {}
		void release_files(storage_error& ec) {}
		void delete_files(storage_error& ec) {}
		void initialize(bool allocate_files, storage_error& ec) {}
		void move_storage(std::string const& save_path, storage_error& ec) {}
		int read(char* buf, int slot, int offset, int size, storage_error& ec) { return size; }
		int write(char const* buf, int slot, int offset, int size, storage_error& ec) { return size; }
		size_type physical_offset(int slot, int offset) { return 0; }
		int readv(file::iovec_t const* bufs, int slot, int offset, int num_bufs, storage_error& ec);
		int writev(file::iovec_t const* bufs, int slot, int offset, int num_bufs, storage_error& ec);
	
		file::aiocb_t* async_readv(file::iovec_t const* bufs, int slot, int offset, int num_bufs
			, boost::function<void(async_handler*)> const& handler);
		file::aiocb_t* async_writev(file::iovec_t const* bufs, int slot, int offset, int num_bufs
			, boost::function<void(async_handler*)> const& handler);

		void move_slot(int src_slot, int dst_slot, storage_error& ec) {}
		void swap_slots(int slot1, int slot2, storage_error& ec) {}
		void swap_slots3(int slot1, int slot2, int slot3, storage_error& ec) {}
		bool verify_resume_data(lazy_entry const& rd, storage_error& error) { return false; }
		void write_resume_data(entry& rd, storage_error& ec) const {}

		int m_piece_size;
	};

	struct disk_io_thread;

	class TORRENT_EXPORT piece_manager
		: public intrusive_ptr_base<piece_manager>
		, boost::noncopyable
	{
	friend class invariant_access;
	friend struct disk_io_thread;
	friend struct block_cache;
	public:

		piece_manager(
			boost::shared_ptr<void> const& torrent
			, boost::intrusive_ptr<torrent_info const> info
			, std::string const& path
			, disk_io_thread& io
			, storage_constructor_type sc
			, storage_mode_t sm
			, std::vector<boost::uint8_t> const& file_prio);

		~piece_manager();

		void raise_fence(boost::function0<void> const& f)
		{
			TORRENT_ASSERT(!m_fence_fun);
			m_fence_fun = f;
		}
		bool has_fence() const { return m_fence_fun; }
		void lower_fence()
		{
			TORRENT_ASSERT(m_fence_fun);
			boost::function0<void> f;
			f.swap(m_fence_fun);
			f();
		}

		boost::intrusive_ptr<torrent_info const> info() const { return m_info; }
		void write_resume_data(entry& rd, storage_error& ec) const;

		void async_finalize_file(int file);

		void async_get_cache_info(cache_status* ret
			, boost::function<void(int, disk_io_job const&)> const& handler);

		void async_check_fastresume(lazy_entry const* resume_data
			, boost::function<void(int, disk_io_job const&)> const& handler);
		
		void async_check_files(boost::function<void(int, disk_io_job const&)> const& handler);

		void async_rename_file(int index, std::string const& name
			, boost::function<void(int, disk_io_job const&)> const& handler);

		void async_read(
			peer_request const& r
			, boost::function<void(int, disk_io_job const&)> const& handler
			, int cache_line_size = 0
			, int cache_expiry = 0);

		void async_read_and_hash(
			peer_request const& r
			, boost::function<void(int, disk_io_job const&)> const& handler
			, int cache_expiry = 0);

		void async_cache(int piece
			, boost::function<void(int, disk_io_job const&)> const& handler
			, int cache_expiry = 0);

		// returns the write queue size
		int async_write(
			peer_request const& r
			, disk_buffer_holder& buffer
			, boost::function<void(int, disk_io_job const&)> const& f);

		void async_hash(int piece, boost::function<void(int, disk_io_job const&)> const& f);

		void async_release_files(
			boost::function<void(int, disk_io_job const&)> const& handler
			= boost::function<void(int, disk_io_job const&)>());

		void abort_disk_io();

		void async_clear_read_cache(
			boost::function<void(int, disk_io_job const&)> const& handler
			= boost::function<void(int, disk_io_job const&)>());

		void async_delete_files(
			boost::function<void(int, disk_io_job const&)> const& handler
			= boost::function<void(int, disk_io_job const&)>());

		void async_move_storage(std::string const& p
			, boost::function<void(int, disk_io_job const&)> const& handler);

		void async_save_resume_data(
			boost::function<void(int, disk_io_job const&)> const& handler);

		enum return_t
		{
			// return values from check_fastresume and check_files
			no_error = 0,
			fatal_disk_error = -1,
			need_full_check = -2,
			disk_check_aborted = -3
		};

		storage_interface* get_storage_impl() { return m_storage.get(); }

	private:

		std::string save_path() const;

		bool verify_resume_data(lazy_entry const& rd, storage_error& e)
		{ return m_storage->verify_resume_data(rd, e); }

		bool is_allocating() const
		{ return m_state == state_expand_pieces; }

		void mark_failed(int index);

		int last_piece() const { return m_last_piece; }

		int slot_for(int piece) const;
		int piece_for(int slot) const;
	
		// helper functions for check_fastresume	
		int check_no_fastresume(storage_error& error);
		int check_init_storage(storage_error& error);
		
		// if error is set and return value is 'no_error' or 'need_full_check'
		// the error message indicates that the fast resume data was rejected
		// if 'fatal_disk_error' is returned, the error message indicates what
		// when wrong in the disk access
		int check_fastresume(lazy_entry const& rd, storage_error& error);

		// this function returns true if the checking is complete
		int check_files(int& current_slot, int& have_piece, storage_error& error);

		bool compact_allocation() const
		{ return m_storage_mode == storage_mode_compact; }

#ifdef TORRENT_DEBUG
		std::string name() const { return m_info->name(); }
#endif

		bool allocate_slots_impl(int num_slots, mutex::scoped_lock& l, bool abort_on_disk = false);

		// updates the ph.h hasher object with the data at the given slot
		// and optionally a 'small hash' as well, the hash for
		// the partial slot. Returns the number of bytes read
		int hash_for_slot(int slot, partial_hash& h, int piece_size, storage_error& ec
			, int small_piece_size = 0, sha1_hash* small_hash = 0);

		void hint_read_impl(int piece_index, int offset, int size);
		file::aiocb_t* read_async_impl(
			file::iovec_t* bufs
			, int piece_index
			, int offset
			, int num_bufs
			, boost::function<void(async_handler*)> const& handler);

		file::aiocb_t* write_async_impl(
			file::iovec_t* bufs
			, int piece_index
			, int offset
			, int num_bufs
			, boost::function<void(async_handler*)> const& handler);

		size_type physical_offset(int piece_index, int offset);

		void finalize_file(int index, storage_error& ec);

		// returns the number of pieces left in the
		// file currently being checked
		int skip_file() const;
		// -1=error 0=ok >0=skip this many pieces
		int check_one_piece(int& have_piece, storage_error& ec);
		int identify_data(
			sha1_hash const& large_hash
			, sha1_hash const& small_hash
			, int current_slot);

		void switch_to_full_mode();
		sha1_hash hash_for_piece_impl(int piece, storage_error& ec, int* readback = 0);

		void release_files_impl(storage_error& ec) { m_storage->release_files(ec); }
		void delete_files_impl(storage_error& ec) { m_storage->delete_files(ec); }
		void rename_file_impl(int index, std::string const& new_filename, storage_error& ec)
		{ m_storage->rename_file(index, new_filename, ec); }

		void move_storage_impl(std::string const& save_path, storage_error& ec);

		int allocate_slot_for_piece(int piece_index);
#ifdef TORRENT_DEBUG
		void check_invariant() const;
#ifdef TORRENT_STORAGE_DEBUG
		void debug_log() const;
#endif
#endif
		boost::intrusive_ptr<torrent_info const> m_info;
		file_storage const& m_files;

		boost::scoped_ptr<storage_interface> m_storage;

		// when set, this storage is blocked for new async
		// operations
		boost::function0<void> m_fence_fun;

		storage_mode_t m_storage_mode;

		// slots that haven't had any file storage allocated
		std::vector<int> m_unallocated_slots;
		// slots that have file storage, but isn't assigned to a piece
		std::vector<int> m_free_slots;

		enum
		{
			has_no_slot = -3 // the piece has no storage
		};

		// maps piece indices to slots. If a piece doesn't
		// have any storage, it is set to 'has_no_slot'
		std::vector<int> m_piece_to_slot;

		enum
		{
			unallocated = -1, // the slot is unallocated
			unassigned = -2   // the slot is allocated but not assigned to a piece
		};

		// maps slots to piece indices, if a slot doesn't have a piece
		// it can either be 'unassigned' or 'unallocated'
		std::vector<int> m_slot_to_piece;

		std::string m_save_path;

		mutable mutex m_mutex;

		enum {
			// the default initial state
			state_none,
			// the file checking is complete
			state_finished,
			// checking the files
			state_full_check,
			// move pieces to their final position
			state_expand_pieces
		} m_state;
		int m_current_slot;
		// used during check. If any piece is found
		// that is not in its final position, this
		// is set to true
		bool m_out_of_place;
		// used to move pieces while expanding
		// the storage from compact allocation
		// to full allocation
		aligned_holder m_scratch_buffer;
		aligned_holder m_scratch_buffer2;
		// the piece that is in the scratch buffer
		int m_scratch_piece;

		// the last piece we wrote to or read from
		int m_last_piece;

		// this is saved in case we need to instantiate a new
		// storage (osed when remapping files)
		storage_constructor_type m_storage_constructor;

		// this maps a piece hash to piece index. It will be
		// build the first time it is used (to save time if it
		// isn't needed)
		std::multimap<sha1_hash, int> m_hash_to_piece;
	
		disk_io_thread& m_io_thread;

		// the reason for this to be a void pointer
		// is to avoid creating a dependency on the
		// torrent. This shared_ptr is here only
		// to keep the torrent object alive until
		// the piece_manager destructs. This is because
		// the torrent_info object is owned by the torrent.
		boost::shared_ptr<void> m_torrent;
	};

}

#endif // TORRENT_STORAGE_HPP_INCLUDED
