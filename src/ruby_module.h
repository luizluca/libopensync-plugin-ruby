/*
 * file-sync - A plugin for the opensync framework
 * Copyright (C) 2004-2005  Armin Bauer <armin.bauer@opensync.org>
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 * 
 */

#ifndef _RUBY_PLUGIN_H
#define _RUBY_PLUGIN_H

#include <opensync/opensync.h>

#include <opensync/opensync-format.h>
#include <opensync/opensync-plugin.h>
#include <opensync/opensync-data.h>
#include <opensync/opensync-helper.h>

#include <sys/stat.h>
#include <stdio.h>
#include <glib.h>
#include <string.h>

#include <libxml/xmlmemory.h>
#include <libxml/parser.h>

#include <ruby.h>

#define RUBY_SCRIPTNAME   "ruby-module"
#define RUBY_BASE_FILE 	  "opensync.rb"
#define RUBY_PLUGIN_CLASS "Opensync::MetaPlugin"
#define RUBY_FORMAT_CLASS "Opensync::MetaFormat"

#ifndef __func__
#define __func__ "unknown_function"
#endif

typedef struct SyncRubyModulePluginData {
	VALUE initialize_fn;
	VALUE finalize_fn;
	VALUE discover_fn;	
	VALUE data;
} OSyncRubyModulePluginData;

typedef struct SyncRubyModuleObjectTypeSinkData {	
	VALUE connect_fn;
	VALUE get_changes_fn;
	VALUE commit_fn;
	VALUE commited_all_fn;
	VALUE read_fn;
	VALUE sync_done_fn;
	VALUE connect_done_fn;
	VALUE disconnect_fn;
	/* this should be user_data but I hope user_data will become data in the future ;) */
	VALUE data;
} OSyncRubyModuleObjectTypeSinkData;

typedef struct SyncRubyModuleObjectFormatData {
        VALUE initialize_fn;
        VALUE finalize_fn;
        VALUE compare_fn;
        VALUE destroy_fn;
        VALUE copy_fn;
        VALUE duplicate_fn;
        VALUE create_fn;
        VALUE print_fn;
        VALUE revision_fn;
        VALUE marshal_fn;
        VALUE demarshal_fn;
        VALUE validate_fn;
// 	VALUE initialize_fn;
// 	VALUE finalize_fn;
// 	VALUE discover_fn;	
	VALUE data;
} OSyncRubyModuleObjectFormatData;

#endif //_RUBY_PLUGIN_H
