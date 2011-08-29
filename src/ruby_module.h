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
#define RUBY_BASE_FILE 	  OPENSYNC_RUBYLIB_DIR "/" "opensync.rb"
// #define RUBY_BASE_FILE 	  "~/prog/opensync/binary-meta/sources/ruby-module/src/opensync.rb"
#define RUBY_PLUGIN_CLASS "Opensync::MetaPlugin"
#define RUBY_FORMAT_CLASS "Opensync::MetaFormat"

#endif //_RUBY_PLUGIN_H
