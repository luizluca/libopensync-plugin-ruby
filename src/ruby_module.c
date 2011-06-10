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

// TODO: deal correctly with error. Ideally, does not pass and use Exception

#include "ruby_module.h"

#include <pthread.h>
#include <ruby/ruby.h>
#include <opensync/opensync-version.h>
#include <assert.h>
#include <stdlib.h>
#include "opensyncRUBY_wrap.c"


RUBY_GLOBAL_SETUP

/* This mutex avoids concurrent use of ruby context (which is prohibit) */
static pthread_mutex_t ruby_context_lock = PTHREAD_MUTEX_INITIALIZER;
static osync_bool      	ruby_initialized = FALSE;

VALUE rb_fcall2_wrapper ( VALUE* params )
{
    //fprintf(stderr,"run %d.%s(...)\n",(uint)params[0],(char*)params[1]);
    return rb_funcall2 ( params[0], rb_intern ( ( char* ) params[1] ), ( int ) params[2], ( VALUE* ) params[3] );
}

VALUE rb_fcall2_protected ( VALUE recv, const char* method, int argc, VALUE* args, int* status )
{
    VALUE params[4];
    params[0]=recv;
    params[1]= ( VALUE ) method;
    params[2]= ( VALUE ) argc;
    params[3]= ( VALUE ) args;
    return rb_protect ( ( VALUE ( * ) ( VALUE ) ) rb_fcall2_wrapper, ( VALUE ) params, status );
}

static char * osync_rubymodule_error_bt ( char* file, const char* func, int line )
{
    VALUE message;
    int state;
    message = rb_protect ( ( VALUE ( * ) ( VALUE ) ) rb_eval_string, ( VALUE ) ( "bt=$!.backtrace; bt[0]=\"#{bt[0]}: #{$!} (#{$!.class})\"; bt.join('\n')" ),&state );
    if ( state!=0 ) {
        message = Qnil;
    }
    return RSTRING_PTR ( message );
}

static void free_plugin_data ( OSyncRubyModulePluginData *data )
{
    // TODO: Free sink userdata?
    if ( data ) {
        if ( data->initialize_fn )
            rb_gc_unregister_address ( & ( data->initialize_fn ) );
        if ( data->finalize_fn )
            rb_gc_unregister_address ( & ( data->finalize_fn ) );
        if ( data->discover_fn )
            rb_gc_unregister_address ( & ( data->discover_fn ) );
        if ( data->data )
            rb_gc_unregister_address ( & ( data->data ) );
    }
    free ( data );
}

/* XXX: All these declarations and the two folowing functions are hackish in order to
 * bypass the missing osync_objtype_sink_get_userdata!
 */
static pthread_mutex_t sinks_userdata_lock = PTHREAD_MUTEX_INITIALIZER;
static OSyncList *sinks_userdata = NULL;
typedef struct Sync2UserData {
    OSyncObjTypeSink *sink;
    void             *data;
} OsyncSink2Data;
static void osync_objtype_sink_set_userdata_xxx ( OSyncObjTypeSink *sink, void *data )
{
    osync_objtype_sink_set_userdata ( sink, data );

    /* XXX: As I do not have sink_get_userdata, keep a list of it */
    pthread_mutex_lock ( &sinks_userdata_lock );

    OsyncSink2Data *found = NULL;
    OSyncList *item = NULL;

    if ( sinks_userdata ) {
        for ( item = sinks_userdata; item; item = item->next ) {
            OsyncSink2Data *sync2user_data = ( OsyncSink2Data* ) item->data;
            if ( sync2user_data->sink == sink ) {
                found = sync2user_data;
                break;
            }
        }
    }
    if ( !found ) {
        if ( data ) {
            found = ( OsyncSink2Data* ) malloc ( sizeof ( OsyncSink2Data ) );
            found->sink = sink;
            found->data = data;
            sinks_userdata = osync_list_prepend ( sinks_userdata, found );
        }
    } else {
        if ( data ) {
            found->data = data;
        } else if ( found ) {
            sinks_userdata = osync_list_delete_link ( sinks_userdata, item );
            free ( found );
        }
    }
    pthread_mutex_unlock ( &sinks_userdata_lock );
}

/* Why this function is not exported in libs? When loaded as plugin, this is avaiable but it is declared in no avaiable .h
 * I dunno why but I'll keep it internal for ruby interfaces */
static void *osync_objtype_sink_get_userdata_xxx ( OSyncObjTypeSink *sink )
{
    pthread_mutex_lock ( &sinks_userdata_lock );
    void* result = NULL;
    if ( !sinks_userdata )
        goto unlock;

    OSyncList *list = sinks_userdata;
    OSyncList *item = NULL;

    for ( item = list; item; item = item->next ) {
        OsyncSink2Data *sync2user_data = ( OsyncSink2Data* ) item->data;
        if ( sync2user_data->sink == sink ) {
            result = sync2user_data->data;
            break;
        }
    }
unlock:
    pthread_mutex_unlock ( &sinks_userdata_lock );
    return result;
}

static void free_objtype_sink_data ( OSyncRubyModuleObjectTypeSinkData *data )
{
    if ( data ) {
        if ( data->commit_fn )
            rb_gc_unregister_address ( & ( data->commit_fn ) );
        if ( data->commited_all_fn )
            rb_gc_unregister_address ( & ( data->commited_all_fn ) );
        if ( data->connect_done_fn )
            rb_gc_unregister_address ( & ( data->connect_done_fn ) );
        if ( data->connect_fn )
            rb_gc_unregister_address ( & ( data->connect_fn ) );
        if ( data->disconnect_fn )
            rb_gc_unregister_address ( & ( data->disconnect_fn ) );
        if ( data->get_changes_fn )
            rb_gc_unregister_address ( & ( data->get_changes_fn ) );
        if ( data->read_fn )
            rb_gc_unregister_address ( & ( data->read_fn ) );
        if ( data->sync_done_fn )
            rb_gc_unregister_address ( & ( data->sync_done_fn ) );
        if ( data->data )
            rb_gc_unregister_address ( & ( data->data ) );
    }
    g_free ( data );
}

static void osync_rubymodule_objtype_sink_connect ( OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, void *data )
{
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p, %p)", __func__, sink, info, ctx, data );
    int status;
    OSyncError *error = 0;
    OSyncRubyModuleObjectTypeSinkData *sink_data = 0;

    sink_data = ( OSyncRubyModuleObjectTypeSinkData* ) data;

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[4];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( sink ), SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[2] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncContext, 0 |  0 );
    args[3] = sink_data->data;
    /*VALUE result = */
    rb_fcall2_protected ( sink_data->connect_fn, "call", 4, args, &status );
    if ( status!=0 ) {
        osync_error_set ( &error, OSYNC_ERROR_GENERIC, "Failed to call sink connect_fn function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s", __func__ );
    return;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_context_report_osyncerror ( ctx, error );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( &error ) );
    osync_error_unref ( &error );
    return;
}

static void osync_rubymodule_objtype_sink_get_changes ( OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, osync_bool slow_sync, void *data )
{
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p, %i, %p)", __func__, sink, info, ctx, slow_sync, data );
    int status;
    OSyncError *error = 0;
    OSyncRubyModuleObjectTypeSinkData *sink_data = 0;

    sink_data = ( OSyncRubyModuleObjectTypeSinkData* ) data;

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[5];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( sink ), SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[2] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncContext, 0 |  0 );
    args[3] = slow_sync==FALSE? Qfalse : Qtrue;
    args[4] = sink_data->data;
    /*VALUE result = */
    rb_fcall2_protected ( sink_data->get_changes_fn, "call", 5, args, &status );
    if ( status!=0 ) {
        osync_error_set ( &error, OSYNC_ERROR_GENERIC, "Failed to call sink get_changes_fn function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s", __func__ );
    return;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_context_report_osyncerror ( ctx, error );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( &error ) );
    osync_error_unref ( &error );
    return;
}

static void osync_rubymodule_objtype_sink_commit ( OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, OSyncChange *change, void *data )
{
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p, %p, %p)", __func__, sink , info, ctx, change, data );
    int status;
    OSyncError *error = 0;
    OSyncRubyModuleObjectTypeSinkData *sink_data = 0;

    sink_data = ( OSyncRubyModuleObjectTypeSinkData* ) data;

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[5];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( sink ), SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[2] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncContext, 0 |  0 );
    args[3] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncChange, 0 |  0 );
    args[4] = sink_data->data;
    /*VALUE result = */
    rb_fcall2_protected ( sink_data->commit_fn, "call", 5, args, &status );
    if ( status!=0 ) {
        osync_error_set ( &error, OSYNC_ERROR_GENERIC, "Failed to call sink commit_fn function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s", __func__ );
    return;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_context_report_osyncerror ( ctx, error );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( &error ) );
    osync_error_unref ( &error );
    return;
}

static void osync_rubymodule_objtype_sink_commited_all ( OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, void *data )
{
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p, %p)", __func__, sink, info, ctx, data );
    int status;
    OSyncError *error = 0;
    OSyncRubyModuleObjectTypeSinkData *sink_data = 0;

    sink_data = ( OSyncRubyModuleObjectTypeSinkData* ) data;

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[4];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( sink ), SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[2] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncContext, 0 |  0 );
    args[3] = sink_data->data;
    /*VALUE result = */
    rb_fcall2_protected ( sink_data->commited_all_fn, "call", 4, args, &status );
    if ( status!=0 ) {
        osync_error_set ( &error, OSYNC_ERROR_GENERIC, "Failed to call sink commited_all function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s", __func__ );
    return;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_context_report_osyncerror ( ctx, error );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( &error ) );
    osync_error_unref ( &error );
    return;
}

static void osync_rubymodule_objtype_sink_read ( OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, OSyncChange *change, void *data )
{
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p, %p, %p)", __func__, sink , info, ctx, change, data );
    int status;
    OSyncError *error = 0;
    OSyncRubyModuleObjectTypeSinkData *sink_data = 0;

    sink_data = ( OSyncRubyModuleObjectTypeSinkData* ) data;

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[5];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( sink ), SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[2] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncContext, 0 |  0 );
    args[3] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncChange, 0 |  0 );
    args[4] = sink_data->data;
    /*VALUE result = */
    rb_fcall2_protected ( sink_data->read_fn, "call", 5, args, &status );
    if ( status!=0 ) {
        osync_error_set ( &error, OSYNC_ERROR_GENERIC, "Failed to call sink read_fn function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s", __func__ );
    return;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_context_report_osyncerror ( ctx, error );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( &error ) );
    osync_error_unref ( &error );
    return;
}

static void osync_rubymodule_objtype_sink_sync_done ( OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, void *data )
{
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p, %p)", __func__, sink, info, ctx, data );
    int status;
    OSyncError *error = 0;
    OSyncRubyModuleObjectTypeSinkData *sink_data = 0;

    sink_data = ( OSyncRubyModuleObjectTypeSinkData* ) data;

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[4];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( sink ), SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[2] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncContext, 0 |  0 );
    args[3] = sink_data->data;
    /*VALUE result = */
    rb_fcall2_protected ( sink_data->sync_done_fn, "call", 4, args, &status );
    if ( status!=0 ) {
        osync_error_set ( &error, OSYNC_ERROR_GENERIC, "Failed to call sink sync_done function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s", __func__ );
    return;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_context_report_osyncerror ( ctx, error );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( &error ) );
    osync_error_unref ( &error );
    return;
}

static void osync_rubymodule_objtype_sink_connect_done ( OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, osync_bool slow_sync, void *data )
{
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p, %i, %p)", __func__, sink, info, ctx, slow_sync, data );
    int status;
    OSyncError *error = 0;
    OSyncRubyModuleObjectTypeSinkData *sink_data = 0;

    sink_data = ( OSyncRubyModuleObjectTypeSinkData* ) data;

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[5];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( sink ), SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[2] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncContext, 0 |  0 );
    args[3] = slow_sync==FALSE? Qfalse : Qtrue;
    args[4] = sink_data->data;
    /*VALUE result = */
    rb_fcall2_protected ( sink_data->disconnect_fn, "call", 5, args, &status );
    if ( status!=0 ) {
        osync_error_set ( &error, OSYNC_ERROR_GENERIC, "Failed to call sink disconnect_fn function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s", __func__ );
    return;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_context_report_osyncerror ( ctx, error );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( &error ) );
    osync_error_unref ( &error );
    return;
}

static void osync_rubymodule_objtype_sink_disconnect ( OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, void *data )
{
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p, %p)", __func__, sink, info, ctx, data );
    int status;
    OSyncError *error = 0;
    OSyncRubyModuleObjectTypeSinkData *sink_data = 0;

    sink_data = ( OSyncRubyModuleObjectTypeSinkData* ) data;

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[4];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( sink ), SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[2] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncContext, 0 |  0 );
    args[3] = sink_data->data;
    /*VALUE result = */
    rb_fcall2_protected ( sink_data->disconnect_fn, "call", 4, args, &status );
    if ( status!=0 ) {
        osync_error_set ( &error, OSYNC_ERROR_GENERIC, "Failed to call sink disconnect_fn function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s", __func__ );
    return;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_context_report_osyncerror ( ctx, error );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( &error ) );
    osync_error_unref ( &error );
    return;
}



/* In initialize, we get the config for the plugin. Here we also must register
 * all _possible_ objtype sinks. */
static void osync_rubymodule_plugin_initialize ( OSyncPlugin *plugin, OSyncPluginInfo *info, OSyncError **error )
{
    int status;
    OSyncRubyModulePluginData *plugin_data = 0;
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p)", __func__, plugin, info, error );

    plugin_data = ( OSyncRubyModulePluginData* ) osync_plugin_get_data ( plugin );

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[1];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    VALUE result = rb_fcall2_protected ( plugin_data->initialize_fn, "call", 1, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call plugin initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    plugin_data->data=result;

    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: %p", __func__, plugin_data );
    return;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return;
}

// SGST: add some parameters in order to free sink datas
static void osync_rubymodule_plugin_finalize ( OSyncPlugin *plugin)
{
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, plugin );

    int status;
    OSyncRubyModulePluginData *plugin_data = ( OSyncRubyModulePluginData * )osync_plugin_get_data(plugin);

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[1];
    args[0] = plugin_data->data;
    /*VALUE result = */
    rb_fcall2_protected ( plugin_data->finalize_fn, "call", 1, args, &status );
    /* there is no error return, no one is interested if finalize fails
    if (status!=0) {
      	osync_error_set(error, OSYNC_ERROR_INITIALIZATION, "Failed to call plugin finalize function!\n%s",
    	  osync_rubymodule_error_bt(__FILE__, __func__,__LINE__));
    goto error;
    } */
    pthread_mutex_unlock ( &ruby_context_lock );

    free_plugin_data ( plugin_data );
    osync_trace ( TRACE_EXIT, "%s", __func__ );
}

/* Here we actually tell opensync which sinks are available. For this plugin, we
 * just report all objtype as available. Since the resource are configured like this. */
static osync_bool osync_rubymodule_plugin_discover (OSyncPlugin *plugin, OSyncPluginInfo *info, OSyncError **error )
{
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p)", __func__, plugin, info, error );
  
    OSyncRubyModulePluginData *plugin_data = ( OSyncRubyModulePluginData * ) osync_plugin_get_data(plugin);
    int status;    

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[2];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[1] = plugin_data->data;
    VALUE result = rb_fcall2_protected ( plugin_data->initialize_fn, "call", 2, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call plugin discover function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    pthread_mutex_unlock ( &ruby_context_lock );

    osync_trace ( TRACE_EXIT, "%s: %i", __func__, result==Qtrue );
    return result==Qtrue;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}


/*
 *  Format
 *
 */

/* XXX: objtype_format does not have a set_data function! */
static pthread_mutex_t objformat_data_lock = PTHREAD_MUTEX_INITIALIZER;
static OSyncList *objformats_data = NULL;
typedef struct ObjectFormat2Data {
    OSyncObjFormat     		*format;
    OSyncRubyModuleObjectFormatData *data;
} OSyncObjectFormat2Data;
static void osync_objformat_set_data ( OSyncObjFormat *format, void *data )
{
    pthread_mutex_lock ( &objformat_data_lock );

    OSyncObjectFormat2Data *found = NULL;
    OSyncList *item = NULL;

    if ( objformats_data ) {
        for ( item = objformats_data; item; item = item->next ) {
            OSyncObjectFormat2Data *sync2user_data = ( OSyncObjectFormat2Data* ) item->data;
            if ( sync2user_data->format == format ) {
                found = sync2user_data;
                break;
            }
        }
    }
    if ( !found ) {
        if ( data ) {
            found = ( OSyncObjectFormat2Data* ) malloc ( sizeof ( OSyncObjectFormat2Data ) );
            found->format = format;
            found->data = data;
            objformats_data = osync_list_prepend ( objformats_data, found );
        }
    } else {
        if ( data ) {
            found->data = data;
        } else if ( found ) {
            objformats_data = osync_list_delete_link ( objformats_data, item );
            free ( found );
        }
    }
    pthread_mutex_unlock ( &objformat_data_lock );
}

static void *osync_objformat_get_data ( OSyncObjFormat *format )
{
    pthread_mutex_lock ( &objformat_data_lock );
    void* result = NULL;
    if ( !objformats_data )
        goto unlock;

    OSyncList *list = objformats_data;
    OSyncList *item = NULL;

    for ( item = list; item; item = item->next ) {
        OSyncObjectFormat2Data *sync2user_data = ( OSyncObjectFormat2Data* ) item->data;
        if ( sync2user_data->format == format ) {
            result = sync2user_data->data;
            break;
        }
    }
unlock:
    pthread_mutex_unlock ( &objformat_data_lock );
    return result;
}

// TODO
static void free_objformat_data(OSyncRubyModuleObjectFormatData *data)
{
    // TODO: cleanup ruby objects
    free(data);
}

static void *osync_rubymodule_objformat_initialize(OSyncObjFormat *format, OSyncError **error)
{
    int status;
    OSyncRubyModuleObjectFormatData *format_data = 0;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    pthread_mutex_lock ( &ruby_context_lock );
    format_data = ( OSyncRubyModuleObjectFormatData* ) osync_objformat_get_data ( format );
    VALUE args[1];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    VALUE result = rb_fcall2_protected ( format_data->initialize_fn, "call", 1, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call plugin initialize function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    format_data->data=result;

    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: %p", __func__, format_data );
    return ( void * ) format_data;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return NULL;
}

static osync_bool osync_rubymodule_objformat_finalize(OSyncObjFormat *format, OSyncError **error)
{
    int status;
    OSyncRubyModuleObjectFormatData *format_data = 0;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    pthread_mutex_lock ( &ruby_context_lock );

    format_data = ( OSyncRubyModuleObjectFormatData* ) osync_objformat_get_data ( format );
    VALUE args[1];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    VALUE result = rb_fcall2_protected ( format_data->finalize_fn, "call", 1, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call plugin finalize function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ((result == Qfalse) || (result == Qnil))
        goto error;

    format_data->data=result;

    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: %p", __func__, format_data );
    return TRUE;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}

// TODO
static OSyncConvCmpResult osync_rubymodule_objformat_compare(OSyncObjFormat *format, const char *leftdata, unsigned int leftsize, const char *rightdata, unsigned int rightsize, void *user_data, OSyncError **error)
{
    int status;
    OSyncRubyModuleObjectFormatData *format_data = 0;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    pthread_mutex_lock ( &ruby_context_lock );

    format_data = ( OSyncRubyModuleObjectFormatData* ) osync_objformat_get_data ( format );
    VALUE args[1];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    VALUE result = rb_fcall2_protected ( format_data->compare_fn, "call", 1, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call plugin initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    format_data->data=result;

    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: %p", __func__, format_data );
    return 0;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return 0;
}

// TODO
static char *osync_rubymodule_objformat_print(OSyncObjFormat *format, const char *data, unsigned int size, void *user_data, OSyncError **error)
{
    int status;
    OSyncRubyModuleObjectFormatData *format_data = 0;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    pthread_mutex_lock ( &ruby_context_lock );

    format_data = ( OSyncRubyModuleObjectFormatData* ) osync_objformat_get_data ( format );
    VALUE args[1];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    VALUE result = rb_fcall2_protected ( format_data->print_fn, "call", 1, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call plugin initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    format_data->data=result;

    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: %p", __func__, format_data );
    return ( void * ) format_data;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return NULL;
}

// TODO
static time_t osync_rubymodule_objformat_revision(OSyncObjFormat *format, const char *data, unsigned int size, void *user_data, OSyncError **error)
{
    int status;
    OSyncRubyModuleObjectFormatData *format_data = 0;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    pthread_mutex_lock ( &ruby_context_lock );

    format_data = ( OSyncRubyModuleObjectFormatData* ) osync_objformat_get_data ( format );
    VALUE args[1];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    VALUE result = rb_fcall2_protected ( format_data->revision_fn, "call", 1, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call plugin initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    format_data->data=result;

    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: %p", __func__, format_data );
    return TRUE;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}

// TODO
static osync_bool osync_rubymodule_objformat_destroy(OSyncObjFormat *format, char *data, unsigned int size, void *user_data, OSyncError **error)
{
    int status;
    OSyncRubyModuleObjectFormatData *format_data = 0;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    pthread_mutex_lock ( &ruby_context_lock );

    format_data = ( OSyncRubyModuleObjectFormatData* ) osync_objformat_get_data ( format );
    VALUE args[1];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    VALUE result = rb_fcall2_protected ( format_data->destroy_fn, "call", 1, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call plugin initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ((result == Qfalse) || (result == Qnil))
        goto error;

    format_data->data=result;

    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: %p", __func__, format_data );
    return TRUE;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}

// TODO
static osync_bool osync_rubymodule_objformat_copy(OSyncObjFormat *format, const char *input, unsigned int inpsize, char **output, unsigned int *outpsize, void *user_data, OSyncError **error)
{
    int status;
    OSyncRubyModuleObjectFormatData *format_data = 0;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    pthread_mutex_lock ( &ruby_context_lock );

    format_data = ( OSyncRubyModuleObjectFormatData* ) osync_objformat_get_data ( format );
    VALUE args[1];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    VALUE result = rb_fcall2_protected ( format_data->copy_fn, "call", 1, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call plugin initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ((result == Qfalse) || (result == Qnil))
        goto error;

    format_data->data=result;

    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: %p", __func__, format_data );
    return TRUE;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}

// TODO
static osync_bool osync_rubymodule_objformat_duplicate(OSyncObjFormat *format, const char *uid, const char *input, unsigned int insize, char **newuid, char **output, unsigned int *outsize, osync_bool *dirty, void *user_data, OSyncError **error)
{
    int status;
    OSyncRubyModuleObjectFormatData *format_data = 0;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    pthread_mutex_lock ( &ruby_context_lock );

    format_data = ( OSyncRubyModuleObjectFormatData* ) osync_objformat_get_data ( format );
    VALUE args[1];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    VALUE result = rb_fcall2_protected ( format_data->duplicate_fn, "call", 1, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call plugin initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ((result == Qfalse) || (result == Qnil))
        goto error;

    format_data->data=result;

    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: %p", __func__, format_data );
    return TRUE;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}

// TODO
static osync_bool osync_rubymodule_objformat_create(OSyncObjFormat *format, char **data, unsigned int *size, void *user_data, OSyncError **error)
{
    int status;
    OSyncRubyModuleObjectFormatData *format_data = 0;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    pthread_mutex_lock ( &ruby_context_lock );

    format_data = ( OSyncRubyModuleObjectFormatData* ) osync_objformat_get_data ( format );
    VALUE args[1];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    VALUE result = rb_fcall2_protected ( format_data->initialize_fn, "call", 1, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call plugin initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ((result == Qfalse) || (result == Qnil))
        goto error;

    format_data->data=result;

    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: %p", __func__, format_data );
    return TRUE;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}

// TODO
static osync_bool osync_rubymodule_objformat_marshal(OSyncObjFormat *format, const char *input, unsigned int inpsize, OSyncMarshal *marshal, void *user_data, OSyncError **error)
{
    int status;
    OSyncRubyModuleObjectFormatData *format_data = 0;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    pthread_mutex_lock ( &ruby_context_lock );

    format_data = ( OSyncRubyModuleObjectFormatData* ) osync_objformat_get_data ( format );
    VALUE args[1];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    VALUE result = rb_fcall2_protected ( format_data->marshal_fn, "call", 1, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call plugin initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ((result == Qfalse) || (result == Qnil))
        goto error;

    format_data->data=result;

    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: %p", __func__, format_data );
    return TRUE;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}

// TODO
static osync_bool osync_rubymodule_objformat_demarshal(OSyncObjFormat *format, OSyncMarshal *marshal, char **output, unsigned int *outpsize, void *user_data, OSyncError **error)
{
    int status;
    OSyncRubyModuleObjectFormatData *format_data = 0;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    pthread_mutex_lock ( &ruby_context_lock );

    format_data = ( OSyncRubyModuleObjectFormatData* ) osync_objformat_get_data ( format );
    VALUE args[1];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    VALUE result = rb_fcall2_protected ( format_data->demarshal_fn, "call", 1, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call plugin initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ((result == Qfalse) || (result == Qnil))
        goto error;

    format_data->data=result;

    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: %p", __func__, format_data );
    return TRUE;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}

// TODO
static osync_bool osync_rubymodule_objformat_validate(OSyncObjFormat *format, const char *data, unsigned int size, void *user_data, OSyncError **error)
{
    int status;
    OSyncRubyModuleObjectFormatData *format_data = 0;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    pthread_mutex_lock ( &ruby_context_lock );

    format_data = ( OSyncRubyModuleObjectFormatData* ) osync_objformat_get_data ( format );
    VALUE args[1];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    VALUE result = rb_fcall2_protected ( format_data->validate_fn, "call", 1, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call plugin initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ((result == Qfalse) || (result == Qnil))
        goto error;

    format_data->data=result;

    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: %p", __func__, format_data );
    return TRUE;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}

void ruby_initialize();

osync_bool get_sync_info ( OSyncPluginEnv *env, OSyncError **error )
{
    int   status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, env );

    pthread_mutex_lock ( &ruby_context_lock );

    ruby_initialize();

    rb_protect ( ( VALUE ( * ) ( VALUE ) ) rb_require, ( VALUE ) RUBY_BASE_FILE, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Failed to load module ruby file '%s'!\n%s",RUBY_BASE_FILE,
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }

    VALUE plugin_class = rb_protect ( ( VALUE ( * ) ( VALUE ) ) rb_path2class, ( VALUE ) RUBY_PLUGIN_CLASS, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Error on finding %s class in %s!\n%s",RUBY_PLUGIN_CLASS, RUBY_BASE_FILE,
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( plugin_class == Qnil ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Class %s not found!\n%s", RUBY_PLUGIN_CLASS,
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }

    VALUE args[1];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( env ), SWIGTYPE_p_OSyncPluginEnv, 0 |  0 );
    VALUE result = rb_fcall2_protected ( plugin_class,"get_sync_info",1, args,&status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Failed to call %s.get_sync_info!\n%s", RUBY_PLUGIN_CLASS,
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }

    if ((result == Qfalse) || (result == Qnil)) {
        goto error;
    }

    pthread_mutex_unlock ( &ruby_context_lock );
    return TRUE;
error:
    osync_trace ( TRACE_ERROR, "Unable to register: %s", osync_error_print ( error ) );
    pthread_mutex_unlock ( &ruby_context_lock );
    return FALSE;
}

osync_bool get_format_info ( OSyncFormatEnv *env, OSyncError **error )
{
    int   status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, env );

    pthread_mutex_lock ( &ruby_context_lock );

    ruby_initialize();

    rb_protect ( ( VALUE ( * ) ( VALUE ) ) rb_require, ( VALUE ) RUBY_BASE_FILE, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Failed to load module ruby file '%s'!\n%s",RUBY_BASE_FILE,
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }

    VALUE plugin_class = rb_protect ( ( VALUE ( * ) ( VALUE ) ) rb_path2class, ( VALUE ) RUBY_FORMAT_CLASS, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Error on finding %s class in %s!\n%s",RUBY_FORMAT_CLASS, RUBY_BASE_FILE,
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( plugin_class == Qnil ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Class %s not found!\n%s", RUBY_FORMAT_CLASS,
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }

    VALUE args[1];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( env ), SWIGTYPE_p_OSyncFormatEnv, 0 |  0 );
    VALUE result = rb_fcall2_protected ( plugin_class,"get_format_info",1, args,&status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Failed to call %s.get_format_info!\n%s", RUBY_FORMAT_CLASS,
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }

    if ((result == Qfalse) || (result == Qnil)) {
        goto error;
    }

    pthread_mutex_unlock ( &ruby_context_lock );
    return TRUE;
error:
    osync_trace ( TRACE_ERROR, "Unable to register: %s", osync_error_print ( error ) );
    pthread_mutex_unlock ( &ruby_context_lock );
    return FALSE;
}

osync_bool get_conversion_info ( OSyncFormatEnv *env, OSyncError **error )
{
    return FALSE;
// 	OSyncObjFormat *file = osync_format_env_find_objformat(env, "file");
// 	OSyncObjFormat *plain = NULL;
// 	OSyncFormatConverter *conv = NULL;
//
// 	if (!file) {
// 		osync_error_set(error, OSYNC_ERROR_GENERIC, "Unable to find file format");
// 		return FALSE;
// 	}
//
// 	plain = osync_format_env_find_objformat(env, "plain");
// 	if (!plain) {
// 		osync_error_set(error, OSYNC_ERROR_GENERIC, "Unable to find plain format");
// 		return FALSE;
// 	}
//
// 	conv = osync_converter_new(OSYNC_CONVERTER_DECAP, file, plain, conv_file_to_plain, error);
// 	if (!conv)
// 		return FALSE;
//
// 	osync_format_env_register_converter(env, conv, error);
// 	osync_converter_unref(conv);
//
// 	conv = osync_converter_new(OSYNC_CONVERTER_ENCAP, plain, file, conv_plain_to_file, error);
// 	if (!conv)
// 		return FALSE;
//
// 	osync_format_env_register_converter(env, conv, error);
// 	osync_converter_unref(conv);
// 	return TRUE;
}


/** Plugin */

VALUE
rb_osync_plugin_ruby_free ( int argc, VALUE *argv, VALUE self )
{
    OSyncPlugin *arg1 = ( OSyncPlugin * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;
    OSyncRubyModulePluginData *data = 0 ;

    if ( ( argc < 1 ) || ( argc > 1 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 1)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncPlugin, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncPlugin *","osync_plugin_get_data", 1, argv[0] ) );
    }
    arg1 = ( OSyncPlugin * ) ( argp1 );
    data = ( OSyncRubyModulePluginData * ) osync_plugin_get_data ( arg1 );
    free_plugin_data ( data );
    osync_plugin_set_data ( arg1,NULL );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_plugin_ruby_init ( int argc, VALUE *argv, VALUE self )
{
    OSyncPlugin *arg1 = ( OSyncPlugin * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;
    OSyncRubyModulePluginData *data = 0 ;

    if ( ( argc < 1 ) || ( argc > 1 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 1)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncPlugin, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncPlugin *","osync_plugin_get_data", 1, argv[0] ) );
    }
    arg1 = ( OSyncPlugin * ) ( argp1 );
    data = ( OSyncRubyModulePluginData* ) osync_plugin_get_data ( arg1 );

    if ( data != NULL ) {
        rb_raise ( rb_eArgError, "Cannot call ruby_init twice for the same object!" );
        SWIG_fail;
    }
    data = ( OSyncRubyModulePluginData* ) malloc ( sizeof ( OSyncRubyModulePluginData ) );
    if ( !data ) {
        rb_raise ( rb_eNoMemError,"Failed to malloc plugin data of %lu bytes", sizeof ( OSyncRubyModulePluginData ) );
        SWIG_fail;
    }
    memset ( data, 0, sizeof ( OSyncRubyModulePluginData ) );
    osync_plugin_set_data ( arg1, ( void* ) data );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_plugin_get_data ( int argc, VALUE *argv, VALUE self )
{
    OSyncPlugin *arg1 = ( OSyncPlugin * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;
    OSyncRubyModulePluginData *data = 0 ;
    VALUE vresult = Qnil;

    if ( ( argc < 1 ) || ( argc > 1 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 1)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncPlugin, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncPlugin *","osync_plugin_get_data", 1, argv[0] ) );
    }
    arg1 = ( OSyncPlugin * ) ( argp1 );
    data = ( OSyncRubyModulePluginData * ) osync_plugin_get_data ( arg1 );
    if ( !data ) {
        rb_raise ( rb_eArgError, "ruby_init not called before this method!" );
        SWIG_fail;
    }
    vresult = data->data;
    if ( !vresult )
        vresult=Qnil;
    return vresult;
fail:
    return Qnil;
}

VALUE
rb_osync_plugin_set_data ( int argc, VALUE *argv, VALUE self )
{
    OSyncPlugin *arg1 = ( OSyncPlugin * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;
    OSyncRubyModulePluginData *data = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncPlugin, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncPlugin *","osync_plugin_set_data", 1, argv[0] ) );
    }
    arg1 = ( OSyncPlugin * ) ( argp1 );

    data = ( OSyncRubyModulePluginData* ) osync_plugin_get_data ( arg1 );
    if ( !data ) {
        rb_raise ( rb_eArgError, "ruby_init not called before this method!" );
        SWIG_fail;
    }

    if ( data->data )
        rb_gc_unregister_address ( & ( data->data ) );

    data->data=argv[1];
    rb_gc_register_address ( & ( data->data ) );

    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_plugin_set_initialize_func ( int argc, VALUE *argv, VALUE self )
{
    OSyncPlugin *arg1 = ( OSyncPlugin * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncPlugin, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncPlugin *","osync_plugin_set_initialize", 1, argv[0] ) );
    }
    arg1 = ( OSyncPlugin * ) ( argp1 );

    OSyncRubyModulePluginData *data = osync_plugin_get_data ( arg1 );
    data->initialize_fn = argv[1];

    osync_plugin_set_initialize_func ( arg1,osync_rubymodule_plugin_initialize );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_plugin_set_finalize_func ( int argc, VALUE *argv, VALUE self )
{
    OSyncPlugin *arg1 = ( OSyncPlugin * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncPlugin, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncPlugin *","osync_plugin_set_finalize", 1, argv[0] ) );
    }
    arg1 = ( OSyncPlugin * ) ( argp1 );

    OSyncRubyModulePluginData *data = osync_plugin_get_data ( arg1 );
    data->finalize_fn = argv[1];

    osync_plugin_set_finalize_func ( arg1,osync_rubymodule_plugin_finalize );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_plugin_set_discover_func ( int argc, VALUE *argv, VALUE self )
{
    OSyncPlugin *arg1 = ( OSyncPlugin * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncPlugin, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncPlugin *","osync_plugin_set_discover", 1, argv[0] ) );
    }
    arg1 = ( OSyncPlugin * ) ( argp1 );

    OSyncRubyModulePluginData *data = osync_plugin_get_data ( arg1 );
    data->discover_fn = argv[1];

    osync_plugin_set_discover_func ( arg1,osync_rubymodule_plugin_discover );
    return Qnil;
fail:
    return Qnil;
}


/** ObjectType Sinks */

VALUE
rb_osync_rubymodule_objtype_sink_ruby_free ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjTypeSink *arg1 = ( OSyncObjTypeSink * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;
    OSyncRubyModuleObjectTypeSinkData *data = 0 ;

    if ( ( argc < 1 ) || ( argc > 1 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 1)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjTypeSink *","osync_objtype_sink_get_data", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjTypeSink * ) ( argp1 );
    data = ( OSyncRubyModuleObjectTypeSinkData * ) osync_objtype_sink_get_userdata_xxx ( arg1 );
    free_objtype_sink_data ( data );
    osync_objtype_sink_set_userdata_xxx ( arg1,NULL );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objtype_sink_ruby_init ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjTypeSink *arg1 = ( OSyncObjTypeSink * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;
    OSyncRubyModuleObjectTypeSinkData *data = 0 ;

    if ( ( argc < 1 ) || ( argc > 1 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 1)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjTypeSink *","osync_objtype_sink_get_data", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjTypeSink * ) ( argp1 );
    data = ( OSyncRubyModuleObjectTypeSinkData* ) osync_objtype_sink_get_userdata_xxx ( arg1 );

    if ( data != NULL ) {
        rb_raise ( rb_eArgError, "Cannot call ruby_init twice for the same object!" );
        SWIG_fail;
    }
    data = ( OSyncRubyModuleObjectTypeSinkData* ) malloc ( sizeof ( OSyncRubyModuleObjectTypeSinkData ) );
    if ( !data ) {
        rb_raise ( rb_eNoMemError,"Failed to malloc objtype_sink data of %lu bytes", sizeof ( OSyncRubyModuleObjectTypeSinkData ) );
        SWIG_fail;
    }
    memset ( data, 0, sizeof ( OSyncRubyModuleObjectTypeSinkData ) );
    osync_objtype_sink_set_userdata_xxx ( arg1, ( void* ) data );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objtype_sink_get_userdata ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjTypeSink *arg1 = ( OSyncObjTypeSink * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;
    OSyncRubyModuleObjectTypeSinkData *data = 0 ;
    VALUE vresult = Qnil;

    if ( ( argc < 1 ) || ( argc > 1 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 1)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjTypeSink *","osync_objtype_sink_get_data", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjTypeSink * ) ( argp1 );
    data = ( OSyncRubyModuleObjectTypeSinkData * ) osync_objtype_sink_get_userdata_xxx ( arg1 );
    if ( !data ) {
        rb_raise ( rb_eArgError, "ruby_init not called before this method!" );
        SWIG_fail;
    }
    vresult = data->data;
    if ( !vresult )
        vresult=Qnil;
    return vresult;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objtype_sink_set_userdata ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjTypeSink *arg1 = ( OSyncObjTypeSink * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;
    OSyncRubyModuleObjectTypeSinkData *data = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjTypeSink *","osync_objtype_sink_set_data", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjTypeSink * ) ( argp1 );

    data = ( OSyncRubyModuleObjectTypeSinkData* ) osync_objtype_sink_get_userdata_xxx ( arg1 );
    if ( !data ) {
        rb_raise ( rb_eArgError, "ruby_init not called before this method!" );
        SWIG_fail;
    }

    if ( data->data )
        rb_gc_unregister_address ( & ( data->data ) );

    data->data=argv[1];
    rb_gc_register_address ( & ( data->data ) );

    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objtype_sink_set_read_func ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjTypeSink *arg1 = ( OSyncObjTypeSink * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjTypeSink *","osync_objtype_sink_set_read_func", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjTypeSink * ) ( argp1 );
    OSyncRubyModuleObjectTypeSinkData *data = osync_objtype_sink_get_userdata_xxx ( arg1 );
    data->read_fn = argv[1];
    osync_objtype_sink_set_read_func ( arg1,osync_rubymodule_objtype_sink_read );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objtype_sink_set_sync_done_func ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjTypeSink *arg1 = ( OSyncObjTypeSink * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjTypeSink *","osync_objtype_sink_set_sync_done_func", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjTypeSink * ) ( argp1 );
    OSyncRubyModuleObjectTypeSinkData *data = osync_objtype_sink_get_userdata_xxx ( arg1 );
    data->sync_done_fn = argv[1];
    osync_objtype_sink_set_sync_done_func ( arg1,osync_rubymodule_objtype_sink_sync_done );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objtype_sink_set_connect_done_func ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjTypeSink *arg1 = ( OSyncObjTypeSink * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjTypeSink *","osync_objtype_sink_set_connect_done_func", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjTypeSink * ) ( argp1 );
    OSyncRubyModuleObjectTypeSinkData *data = osync_objtype_sink_get_userdata_xxx ( arg1 );
    data->connect_done_fn = argv[1];
    osync_objtype_sink_set_connect_done_func ( arg1,osync_rubymodule_objtype_sink_connect_done );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objtype_sink_set_disconnect_func ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjTypeSink *arg1 = ( OSyncObjTypeSink * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjTypeSink *","osync_objtype_sink_set_disconnect_func", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjTypeSink * ) ( argp1 );

    OSyncRubyModuleObjectTypeSinkData *data = osync_objtype_sink_get_userdata_xxx ( arg1 );
    data->disconnect_fn = argv[1];
    osync_objtype_sink_set_disconnect_func ( arg1,osync_rubymodule_objtype_sink_disconnect );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objtype_sink_set_connect_func ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjTypeSink *arg1 = ( OSyncObjTypeSink * ) 0 ;
    OSyncSinkConnectFn arg2 = ( OSyncSinkConnectFn ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjTypeSink *","osync_objtype_sink_set_connect_func", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjTypeSink * ) ( argp1 );
    OSyncRubyModuleObjectTypeSinkData *data = osync_objtype_sink_get_userdata_xxx ( arg1 );
    data->connect_fn = argv[1];
    osync_objtype_sink_set_committed_all_func ( arg1,osync_rubymodule_objtype_sink_connect );
    return Qnil;
    osync_objtype_sink_set_connect_func ( arg1,arg2 );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objtype_sink_set_get_changes_func ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjTypeSink *arg1 = ( OSyncObjTypeSink * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjTypeSink *","osync_objtype_sink_set_get_changes_func", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjTypeSink * ) ( argp1 );
    OSyncRubyModuleObjectTypeSinkData *data = osync_objtype_sink_get_userdata_xxx ( arg1 );
    data->get_changes_fn = argv[1];
    osync_objtype_sink_set_get_changes_func ( arg1, osync_rubymodule_objtype_sink_get_changes );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objtype_sink_set_commit_func ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjTypeSink *arg1 = ( OSyncObjTypeSink * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjTypeSink *","osync_objtype_sink_set_commit_func", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjTypeSink * ) ( argp1 );
    OSyncRubyModuleObjectTypeSinkData *data = osync_objtype_sink_get_userdata_xxx ( arg1 );
    data->commit_fn = argv[1];
    osync_objtype_sink_set_commit_func ( arg1,osync_rubymodule_objtype_sink_commit );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objtype_sink_set_committed_all_func ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjTypeSink *arg1 = ( OSyncObjTypeSink * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjTypeSink *","osync_objtype_sink_set_committed_all_func", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjTypeSink * ) ( argp1 );
    OSyncRubyModuleObjectTypeSinkData *data = osync_objtype_sink_get_userdata_xxx ( arg1 );
    data->commited_all_fn = argv[1];
    osync_objtype_sink_set_committed_all_func ( arg1,osync_rubymodule_objtype_sink_commited_all );
    return Qnil;
fail:
    return Qnil;
}

/** ObjectFormat */

VALUE
rb_osync_objformat_ruby_free ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjFormat *arg1 = ( OSyncObjFormat * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;
    OSyncRubyModuleObjectFormatData *data = 0 ;

    if ( ( argc < 1 ) || ( argc > 1 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 1)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjFormat *","osync_objformat_get_data", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjFormat * ) ( argp1 );
    data = ( OSyncRubyModuleObjectFormatData * ) osync_objformat_get_data ( arg1 );
    free_objformat_data ( data );
    osync_objformat_set_data ( arg1,NULL );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_objformat_ruby_init ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjFormat *arg1 = ( OSyncObjFormat * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;
    OSyncRubyModuleObjectFormatData *data = 0 ;

    if ( ( argc < 1 ) || ( argc > 1 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 1)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjFormat *","osync_objformat_get_data", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjFormat * ) ( argp1 );
    data = ( OSyncRubyModuleObjectFormatData* ) osync_objformat_get_data ( arg1 );

    if ( data != NULL ) {
        rb_raise ( rb_eArgError, "Cannot call ruby_init twice for the same object!" );
        SWIG_fail;
    }
    data = ( OSyncRubyModuleObjectFormatData* ) malloc ( sizeof ( OSyncRubyModuleObjectFormatData ) );
    if ( !data ) {
        rb_raise ( rb_eNoMemError,"Failed to malloc objformat data of %lu bytes", sizeof ( OSyncRubyModuleObjectFormatData ) );
        SWIG_fail;
    }
    memset ( data, 0, sizeof ( OSyncRubyModuleObjectFormatData ) );
    osync_objformat_set_data ( arg1, ( void* ) data );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_objformat_get_data ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjFormat *arg1 = ( OSyncObjFormat * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;
    OSyncRubyModuleObjectFormatData *data = 0 ;
    VALUE vresult = Qnil;

    if ( ( argc < 1 ) || ( argc > 1 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 1)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjFormat *","osync_objformat_get_data", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjFormat * ) ( argp1 );
    data = ( OSyncRubyModuleObjectFormatData * ) osync_objformat_get_data ( arg1 );
    if ( !data ) {
        rb_raise ( rb_eArgError, "ruby_init not called before this method!" );
        SWIG_fail;
    }
    vresult = data->data;
    if ( !vresult )
        vresult=Qnil;
    return vresult;
fail:
    return Qnil;
}

VALUE
rb_osync_objformat_set_data ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjFormat *arg1 = ( OSyncObjFormat * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;
    OSyncRubyModuleObjectFormatData *data = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjFormat *","osync_objformat_set_data", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjFormat * ) ( argp1 );

    data = ( OSyncRubyModuleObjectFormatData* ) osync_objformat_get_data ( arg1 );
    if ( !data ) {
        rb_raise ( rb_eArgError, "ruby_init not called before this method!" );
        SWIG_fail;
    }

    if ( data->data )
        rb_gc_unregister_address ( & ( data->data ) );

    data->data=argv[1];
    rb_gc_register_address ( & ( data->data ) );

    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objformat_set_initialize_func ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjFormat *arg1 = ( OSyncObjFormat * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjFormat *","rb_osync_rubymodule_objformat_set_initialize_func", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjFormat * ) ( argp1 );
    OSyncRubyModuleObjectFormatData *data = osync_objformat_get_data ( arg1 );
    data->initialize_fn = argv[1];
    osync_objformat_set_initialize_func (arg1, osync_rubymodule_objformat_initialize);
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objformat_set_finalize_func ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjFormat *arg1 = ( OSyncObjFormat * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjFormat *","rb_osync_rubymodule_objformat_set_finalize_func", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjFormat * ) ( argp1 );
    OSyncRubyModuleObjectFormatData *data = osync_objformat_get_data ( arg1 );
    data->finalize_fn = argv[1];
    osync_objformat_set_finalize_func (arg1, osync_rubymodule_objformat_finalize);
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objformat_set_compare_func ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjFormat *arg1 = ( OSyncObjFormat * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjFormat *","rb_osync_rubymodule_objformat_set_compare_func", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjFormat * ) ( argp1 );
    OSyncRubyModuleObjectFormatData *data = osync_objformat_get_data ( arg1 );
    data->compare_fn = argv[1];
    osync_objformat_set_compare_func (arg1, osync_rubymodule_objformat_compare);
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objformat_set_destroy_func ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjFormat *arg1 = ( OSyncObjFormat * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjFormat *","rb_osync_rubymodule_objformat_set_destroy_func", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjFormat * ) ( argp1 );
    OSyncRubyModuleObjectFormatData *data = osync_objformat_get_data ( arg1 );
    data->destroy_fn = argv[1];
    osync_objformat_set_destroy_func (arg1, osync_rubymodule_objformat_destroy);
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objformat_set_copy_func ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjFormat *arg1 = ( OSyncObjFormat * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjFormat *","rb_osync_rubymodule_objformat_set_copy_func", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjFormat * ) ( argp1 );
    OSyncRubyModuleObjectFormatData *data = osync_objformat_get_data ( arg1 );
    data->copy_fn = argv[1];
    osync_objformat_set_copy_func (arg1, osync_rubymodule_objformat_copy);
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objformat_set_duplicate_func ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjFormat *arg1 = ( OSyncObjFormat * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjFormat *","rb_osync_rubymodule_objformat_set_duplicate_func", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjFormat * ) ( argp1 );
    OSyncRubyModuleObjectFormatData *data = osync_objformat_get_data ( arg1 );
    data->duplicate_fn = argv[1];
    osync_objformat_set_duplicate_func (arg1, osync_rubymodule_objformat_duplicate);
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objformat_set_create_func ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjFormat *arg1 = ( OSyncObjFormat * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjFormat *","rb_osync_rubymodule_objformat_set_create_func", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjFormat * ) ( argp1 );
    OSyncRubyModuleObjectFormatData *data = osync_objformat_get_data ( arg1 );
    data->create_fn = argv[1];
    osync_objformat_set_create_func (arg1, osync_rubymodule_objformat_create);
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objformat_set_print_func ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjFormat *arg1 = ( OSyncObjFormat * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjFormat *","rb_osync_rubymodule_objformat_set_print_func", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjFormat * ) ( argp1 );
    OSyncRubyModuleObjectFormatData *data = osync_objformat_get_data ( arg1 );
    data->print_fn = argv[1];
    osync_objformat_set_print_func (arg1, osync_rubymodule_objformat_print);
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objformat_set_revision_func ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjFormat *arg1 = ( OSyncObjFormat * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjFormat *","rb_osync_rubymodule_objformat_set_revision_func", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjFormat * ) ( argp1 );
    OSyncRubyModuleObjectFormatData *data = osync_objformat_get_data ( arg1 );
    data->revision_fn = argv[1];
    osync_objformat_set_revision_func (arg1, osync_rubymodule_objformat_revision);
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objformat_set_marshal_func ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjFormat *arg1 = ( OSyncObjFormat * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjFormat *","rb_osync_rubymodule_objformat_set_marshal_func", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjFormat * ) ( argp1 );
    OSyncRubyModuleObjectFormatData *data = osync_objformat_get_data ( arg1 );
    data->marshal_fn = argv[1];
    osync_objformat_set_marshal_func (arg1, osync_rubymodule_objformat_marshal);
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objformat_set_demarshal_func ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjFormat *arg1 = ( OSyncObjFormat * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjFormat *","rb_osync_rubymodule_objformat_set_demarshal_func", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjFormat * ) ( argp1 );
    OSyncRubyModuleObjectFormatData *data = osync_objformat_get_data ( arg1 );
    data->demarshal_fn = argv[1];
    osync_objformat_set_demarshal_func (arg1, osync_rubymodule_objformat_demarshal);
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objformat_set_validate_func ( int argc, VALUE *argv, VALUE self )
{
    OSyncObjFormat *arg1 = ( OSyncObjFormat * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjFormat *","rb_osync_rubymodule_objformat_set_validate_func", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjFormat * ) ( argp1 );
    OSyncRubyModuleObjectFormatData *data = osync_objformat_get_data ( arg1 );
    data->validate_fn = argv[1];
    osync_objformat_set_validate_func (arg1, osync_rubymodule_objformat_validate);
    return Qnil;
fail:
    return Qnil;
}

void ruby_initialize()
{
    if ( ruby_initialized )
        return;

    ruby_initialized=TRUE;

    /* Initialize Ruby env */
    RUBY_INIT_STACK;
    ruby_init();
    // TODO add custom path
    ruby_init_loadpath();
    ruby_script ( RUBY_SCRIPTNAME );
    // SWIG initialize (include the module)
    Init_opensync();

    // Include custom made methods (should it be inside opensync.i? I don't think so)
    // Those replace set/get_*data and set_*_func
    rb_define_module_function ( mOpensync, "osync_plugin_get_data", rb_osync_plugin_get_data, -1 );
    rb_define_module_function ( mOpensync, "osync_plugin_set_data", rb_osync_plugin_set_data, -1 );
    rb_define_module_function ( mOpensync, "osync_plugin_ruby_init", rb_osync_plugin_ruby_init, -1 );
    rb_define_module_function ( mOpensync, "osync_plugin_ruby_free", rb_osync_plugin_ruby_free, -1 );
    rb_define_module_function ( mOpensync, "osync_plugin_set_initialize_func", rb_osync_plugin_set_initialize_func, -1 );
    rb_define_module_function ( mOpensync, "osync_plugin_set_finalize_func", rb_osync_plugin_set_finalize_func, -1 );
    rb_define_module_function ( mOpensync, "osync_plugin_set_discover_func", rb_osync_plugin_set_discover_func, -1 );

    rb_define_module_function ( mOpensync, "osync_objtype_sink_ruby_init", rb_osync_rubymodule_objtype_sink_ruby_init, -1 );
    rb_define_module_function ( mOpensync, "osync_objtype_sink_ruby_free", rb_osync_rubymodule_objtype_sink_ruby_free, -1 );
    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_userdata", rb_osync_rubymodule_objtype_sink_set_userdata, -1 );
    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_connect_func", rb_osync_rubymodule_objtype_sink_set_connect_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_get_changes_func", rb_osync_rubymodule_objtype_sink_set_get_changes_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_commit_func", rb_osync_rubymodule_objtype_sink_set_commit_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_committed_all_func", rb_osync_rubymodule_objtype_sink_set_committed_all_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_read_func", rb_osync_rubymodule_objtype_sink_set_read_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_sync_done_func", rb_osync_rubymodule_objtype_sink_set_sync_done_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_connect_done_func", rb_osync_rubymodule_objtype_sink_set_connect_done_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_disconnect_func", rb_osync_rubymodule_objtype_sink_set_disconnect_func, -1 );

    rb_define_module_function ( mOpensync, "osync_objformat_ruby_init", rb_osync_objformat_ruby_init, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_ruby_free", rb_osync_objformat_ruby_free, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_get_data",  rb_osync_objformat_get_data, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_set_data" , rb_osync_objformat_set_data, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_set_initialize_func", rb_osync_rubymodule_objformat_set_initialize_func, -1);
    rb_define_module_function ( mOpensync, "osync_objformat_set_finalize_func", rb_osync_rubymodule_objformat_set_finalize_func, -1);
    rb_define_module_function ( mOpensync, "osync_objformat_set_compare_func", rb_osync_rubymodule_objformat_set_compare_func, -1);
    rb_define_module_function ( mOpensync, "osync_objformat_set_destroy_func", rb_osync_rubymodule_objformat_set_destroy_func, -1);
    rb_define_module_function ( mOpensync, "osync_objformat_set_copy_func", rb_osync_rubymodule_objformat_set_copy_func, -1);
    rb_define_module_function ( mOpensync, "osync_objformat_set_duplicate_func", rb_osync_rubymodule_objformat_set_duplicate_func, -1);
    rb_define_module_function ( mOpensync, "osync_objformat_set_create_func", rb_osync_rubymodule_objformat_set_create_func, -1);
    rb_define_module_function ( mOpensync, "osync_objformat_set_print_func", rb_osync_rubymodule_objformat_set_print_func, -1);
    rb_define_module_function ( mOpensync, "osync_objformat_set_revision_func", rb_osync_rubymodule_objformat_set_revision_func, -1);
    rb_define_module_function ( mOpensync, "osync_objformat_set_marshal_func", rb_osync_rubymodule_objformat_set_marshal_func, -1);
    rb_define_module_function ( mOpensync, "osync_objformat_set_demarshal_func", rb_osync_rubymodule_objformat_set_demarshal_func, -1);
    rb_define_module_function ( mOpensync, "osync_objformat_set_validate_func", rb_osync_rubymodule_objformat_set_validate_func, -1);
}

int get_version ( void )
{
    return 1;
}


/* Suggestions:
 *
* make all _new function behave the same: only malloc or malloc and
also define mandatory attributes.
* if _new only mallocs, create the necessary setters and getters for
each of the attributes
* add a set/get_data to objformat.
* Some callbacks returns values while others not. Why return
osync_bool if error is enough to tell if the function
worked? Why force initialization to return a void* if, maybe, the
initialization is not used to create a struct for data?
* Choose one unique data field name. I found three: user_data, userdata and data
* Plugin callback define methods in plugin
(set_initialize/finalize/discovery) does not have a suffix like in
sink and others. Maybe a osync_plugin_set_initialize_func would sound
better.
 * Is there any difference in osync_format and osync_objformat? I know
that there is only osync_format_env but this format isn't really
objformat?
* osync_objtype_main_sink_new and osync_objtype_sink_new
 */
