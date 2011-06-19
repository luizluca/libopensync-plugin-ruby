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

// TODO: study ruby memory managment and fix any memory leaks

#include "ruby_module.h"

#include <pthread.h>
#include <ruby/ruby.h>
#include <opensync/opensync-version.h>
#include <assert.h>
#include <stdlib.h>
#include <glib.h>
#include "opensyncRUBY_wrap.c"

#define RBOOL(value) ((value==Qfalse) || (value==Qnil) ? FALSE : TRUE)
#define BOOLR(value) (value==FALSE ? Qfalse : Qtrue)

#define IS_BOOL(value)   ((value==Qfalse) || (value==Qtrue))
/* Check_Type(val, T_STRING) raises exception but I these macros are
 * used in not-protected code */
#define IS_STRING(value) (TYPE(value)==T_STRING)
#define IS_ARRAY(value)  (TYPE(value)==T_ARRAY)
#define IS_FIXNUM(value) (FIXNUM_P(value))
#define IS_TIME(value)   (TYPE(value)==rb_cTime)

#define CAST_VALUE(value)     (value==NULL?Qnil:(VALUE)value)

RUBY_GLOBAL_SETUP

/* This mutex avoids concurrent use of ruby context (which is prohibit) */
static pthread_mutex_t ruby_context_lock = PTHREAD_MUTEX_INITIALIZER;
static osync_bool      ruby_initialized = FALSE;

VALUE rb_fcall2_wrapper ( VALUE* params ) {
    //fprintf(stderr,"run %d.%s(...)\n",(uint)params[0],(char*)params[1]);
    return rb_funcall2 ( params[0], rb_intern ( ( char* ) params[1] ), ( int ) params[2], ( VALUE* ) params[3] );
}

VALUE rb_fcall2_protected ( VALUE recv, const char* method, int argc, VALUE* args, int* status ) {
    VALUE params[4];
    params[0]=recv;
    params[1]= ( VALUE ) method;
    params[2]= ( VALUE ) argc;
    params[3]= ( VALUE ) args;
    return rb_protect ( ( VALUE ( * ) ( VALUE ) ) rb_fcall2_wrapper, ( VALUE ) params, status );
}

static char * osync_rubymodule_error_bt ( char* file, const char* func, int line ) {
    VALUE message;
    int state;
    message = rb_protect ( ( VALUE ( * ) ( VALUE ) ) rb_eval_string, ( VALUE ) ( "bt=$!.backtrace; bt[0]=\"#{bt[0]}: #{$!} (#{$!.class})\"; bt.join('\n')" ),&state );
    if ( state!=0 ) {
        message = Qnil;
    }
    return RSTRING_PTR ( message );
}

GHashTable *rubymodule_data;
static pthread_mutex_t rubymodule_data_lock = PTHREAD_MUTEX_INITIALIZER;

void osync_rubymodule_set_data ( void* ptr, char const *key, VALUE data ) {
    GHashTable *ptr_data;
    VALUE saved_data;

//     fprintf(stderr, "%p[\"%s\"@%p]  = %lu\n", ptr, key, &key, data);

    pthread_mutex_lock ( &rubymodule_data_lock );

    ptr_data = ( GHashTable* ) g_hash_table_lookup ( rubymodule_data, ptr );
    if ( ptr_data == NULL ) {
        ptr_data = g_hash_table_new_full ( &g_str_hash, &g_str_equal, NULL, ( GDestroyNotify ) rb_gc_unregister_address );
        g_hash_table_insert ( rubymodule_data, ptr, ptr_data );
    }

    /* Free the value if present */
    g_hash_table_remove ( ptr_data, key );

    if ( data != Qnil ) {
        g_hash_table_insert ( ptr_data, ( char* ) key, ( void* ) data );
        rb_gc_register_address ( &data );
    }

    pthread_mutex_unlock ( &rubymodule_data_lock );
}

VALUE osync_rubymodule_get_data ( void* ptr, char const *key ) {
    GHashTable *ptr_data;
    VALUE saved_data;

//     fprintf(stderr, "%p[\"%s\"@%p]-> ", ptr, key, &key);

    ptr_data = ( GHashTable* ) g_hash_table_lookup ( rubymodule_data, ptr );
    if ( ptr_data == NULL ) {
// 	fprintf(stderr, "no values\n");
        return Qnil;
    }

    saved_data = ( VALUE ) g_hash_table_lookup ( ptr_data, key );
    if ( !saved_data ) {
// 	fprintf(stderr, "%s not found\n", key);
        return Qnil;
    }

//     fprintf(stderr, "%lu\n", ptr, key, saved_data);
    return saved_data;
}

void osync_rubymodule_clean_data ( void* ptr ) {
    g_hash_table_remove ( rubymodule_data, ptr );
}

VALUE rb_osync_rubymodule_get_data ( int argc, VALUE *argv, VALUE self ) {
    void *ptr = 0;
    char const *key;
    int res1 = 0 ;
    VALUE vresult = Qnil;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &ptr, 0 , 0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "void*", "osync_rubymodule_get_ptr_data", 1, argv[0] ) );
    }
    key = RSTRING_PTR ( argv[1] );
    vresult = osync_rubymodule_get_data ( ptr, key );
    return vresult;
fail:
    return Qnil;
}

VALUE rb_osync_rubymodule_set_data ( int argc, VALUE *argv, VALUE self ) {
    void *ptr = 0;
    char const *key;
    int res1 = 0 ;
    VALUE vresult = Qnil;

    if ( ( argc < 3 ) || ( argc > 3 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 3)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &ptr, 0 , 0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "void*", "osync_rubymodule_get_ptr_data", 1, argv[0] ) );
    }
    key = RSTRING_PTR ( argv[1] );
    osync_rubymodule_set_data ( ptr, key, argv[2] );
    return Qnil;
fail:
    return Qnil;
}

VALUE rb_osync_rubymodule_clean_data ( int argc, VALUE *argv, VALUE self ) {
    void *ptr = 0;
    char const *key;
    int res1 = 0 ;
    VALUE vresult = Qnil;

    if ( ( argc < 1 ) || ( argc > 1 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 1)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &ptr, 0 , 0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "void*", "osync_rubymodule_get_ptr_data", 1, argv[0] ) );
    }
    osync_rubymodule_clean_data ( ptr );
    return Qnil;
fail:
    return Qnil;
}

static void free_plugin_data ( VALUE data ) {
    rb_gc_unregister_address ( &data );
}

// /* XXX: All these declarations and the two folowing functions are hackish in order to
//  * bypass the missing osync_objtype_sink_get_userdata!
//  */
// static pthread_mutex_t sinks_userdata_lock = PTHREAD_MUTEX_INITIALIZER;
// static OSyncList *sinks_userdata = NULL;
// typedef struct Sync2UserData {
//     OSyncObjTypeSink *sink;
//     void             *data;
// } OsyncSink2Data;
// static void osync_objtype_sink_set_userdata_xxx ( OSyncObjTypeSink *sink, void *data )
// {
//     osync_objtype_sink_set_userdata ( sink, data );
//
//     /* XXX: As I do not have sink_get_userdata, keep a list of it */
//     pthread_mutex_lock ( &sinks_userdata_lock );
//
//     OsyncSink2Data *found = NULL;
//     OSyncList *item = NULL;
//
//     if ( sinks_userdata ) {
//         for ( item = sinks_userdata; item; item = item->next ) {
//             OsyncSink2Data *sync2user_data = ( OsyncSink2Data* ) item->data;
//             if ( sync2user_data->sink == sink ) {
//                 found = sync2user_data;
//                 break;
//             }
//         }
//     }
//     if ( !found ) {
//         if ( data ) {
//             found = ( OsyncSink2Data* ) malloc ( sizeof ( OsyncSink2Data ) );
//             found->sink = sink;
//             found->data = data;
//             sinks_userdata = osync_list_prepend ( sinks_userdata, found );
//         }
//     } else {
//         if ( data ) {
//             found->data = data;
//         } else if ( found ) {
//             sinks_userdata = osync_list_delete_link ( sinks_userdata, item );
//             free ( found );
//         }
//     }
//     pthread_mutex_unlock ( &sinks_userdata_lock );
// }
//
// /* Why this function is not exported in libs? When loaded as plugin, this is avaiable but it is declared in no avaiable .h
//  * I dunno why but I'll keep it internal for ruby interfaces */
// static void *osync_objtype_sink_get_userdata_xxx ( OSyncObjTypeSink *sink )
// {
//     pthread_mutex_lock ( &sinks_userdata_lock );
//     void* result = NULL;
//     if ( !sinks_userdata )
//         goto unlock;
//
//     OSyncList *list = sinks_userdata;
//     OSyncList *item = NULL;
//
//     for ( item = list; item; item = item->next ) {
//         OsyncSink2Data *sync2user_data = ( OsyncSink2Data* ) item->data;
//         if ( sync2user_data->sink == sink ) {
//             result = sync2user_data->data;
//             break;
//         }
//     }
// unlock:
//     pthread_mutex_unlock ( &sinks_userdata_lock );
//     return result;
// }
//
// static void free_objtype_sink_data ( OSyncRubyModuleObjectTypeSinkData *data )
// {
//     if ( data ) {
//         if ( data->commit_fn )
//             rb_gc_unregister_address ( & ( data->commit_fn ) );
//         if ( data->commited_all_fn )
//             rb_gc_unregister_address ( & ( data->commited_all_fn ) );
//         if ( data->connect_done_fn )
//             rb_gc_unregister_address ( & ( data->connect_done_fn ) );
//         if ( data->connect_fn )
//             rb_gc_unregister_address ( & ( data->connect_fn ) );
//         if ( data->disconnect_fn )
//             rb_gc_unregister_address ( & ( data->disconnect_fn ) );
//         if ( data->get_changes_fn )
//             rb_gc_unregister_address ( & ( data->get_changes_fn ) );
//         if ( data->read_fn )
//             rb_gc_unregister_address ( & ( data->read_fn ) );
//         if ( data->sync_done_fn )
//             rb_gc_unregister_address ( & ( data->sync_done_fn ) );
//         if ( data->data )
//             rb_gc_unregister_address ( & ( data->data ) );
//     }
//     g_free ( data );
// }

static void osync_rubymodule_objtype_sink_connect ( OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, void *data ) {
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p, %p)", __func__, sink, info, ctx, data );
    int status;
    OSyncError *error = 0;

    VALUE callback = osync_rubymodule_get_data ( sink, "connect_func" );
    VALUE ruby_data = CAST_VALUE(data);

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[4];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( sink ), SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[2] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncContext, 0 |  0 );
    args[3] = ruby_data;
    /*VALUE result = */
    rb_fcall2_protected ( callback, "call", 4, args, &status );
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

static void osync_rubymodule_objtype_sink_get_changes ( OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, osync_bool slow_sync, void *data ) {
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p, %i, %p)", __func__, sink, info, ctx, slow_sync, data );
    int status;
    OSyncError *error = 0;

    VALUE callback = osync_rubymodule_get_data ( sink, "get_changes_func" );
    VALUE ruby_data = data? ( VALUE ) data: Qnil;

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[5];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( sink ), SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[2] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncContext, 0 |  0 );
    args[3] = BOOLR ( slow_sync );
    args[4] = ruby_data;
    /*VALUE result = */
    rb_fcall2_protected ( callback, "call", 5, args, &status );
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

static void osync_rubymodule_objtype_sink_commit ( OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, OSyncChange *change, void *data ) {
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p, %p, %p)", __func__, sink , info, ctx, change, data );
    int status;
    OSyncError *error = 0;
    VALUE callback = osync_rubymodule_get_data ( sink, "commit_func" );
    VALUE ruby_data = data? ( VALUE ) data: Qnil;

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[5];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( sink ), SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[2] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncContext, 0 |  0 );
    args[3] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncChange, 0 |  0 );
    args[4] = ruby_data;
    /*VALUE result = */
    rb_fcall2_protected ( callback, "call", 5, args, &status );
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

static void osync_rubymodule_objtype_sink_commited_all ( OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, void *data ) {
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p, %p)", __func__, sink, info, ctx, data );
    int status;
    OSyncError *error = 0;
    VALUE callback = osync_rubymodule_get_data ( sink, "commited_all" );
    VALUE ruby_data = data? ( VALUE ) data: Qnil;

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[4];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( sink ), SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[2] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncContext, 0 |  0 );
    args[3] = ruby_data;
    /*VALUE result = */
    rb_fcall2_protected ( callback, "call", 4, args, &status );
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

static void osync_rubymodule_objtype_sink_read ( OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, OSyncChange *change, void *data ) {
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p, %p, %p)", __func__, sink , info, ctx, change, data );
    int status;
    OSyncError *error = 0;
    VALUE callback = osync_rubymodule_get_data ( sink, "read_func" );
    VALUE ruby_data = data? ( VALUE ) data: Qnil;

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[5];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( sink ), SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[2] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncContext, 0 |  0 );
    args[3] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncChange, 0 |  0 );
    args[4] = ruby_data;
    /*VALUE result = */
    rb_fcall2_protected ( callback, "call", 5, args, &status );
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

static void osync_rubymodule_objtype_sink_sync_done ( OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, void *data ) {
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p, %p)", __func__, sink, info, ctx, data );
    int status;
    OSyncError *error = 0;

    VALUE callback = osync_rubymodule_get_data ( sink, "sync_done" );
    VALUE ruby_data = data? ( VALUE ) data: Qnil;

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[4];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( sink ), SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[2] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncContext, 0 |  0 );
    args[3] = ruby_data;
    /*VALUE result = */
    rb_fcall2_protected ( callback, "call", 4, args, &status );
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

static void osync_rubymodule_objtype_sink_connect_done ( OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, osync_bool slow_sync, void *data ) {
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p, %i, %p)", __func__, sink, info, ctx, slow_sync, data );
    int status;
    OSyncError *error = 0;

    VALUE callback = osync_rubymodule_get_data ( sink, "connect_done" );
    VALUE ruby_data = data? ( VALUE ) data: Qnil;

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[5];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( sink ), SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[2] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncContext, 0 |  0 );
    args[3] = BOOLR ( slow_sync );
    args[4] = ruby_data;
    /*VALUE result = */
    rb_fcall2_protected ( callback, "call", 5, args, &status );
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

static void osync_rubymodule_objtype_sink_disconnect ( OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, void *data ) {
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p, %p)", __func__, sink, info, ctx, data );
    int status;
    OSyncError *error = 0;
    VALUE callback = osync_rubymodule_get_data ( sink, "disconnect" );
    VALUE ruby_data = data? ( VALUE ) data: Qnil;

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[4];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( sink ), SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[2] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncContext, 0 |  0 );
    args[3] = ruby_data;
    /*VALUE result = */
    rb_fcall2_protected ( callback, "call", 4, args, &status );
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
static void *osync_rubymodule_plugin_initialize ( OSyncPlugin *plugin, OSyncPluginInfo *info, OSyncError **error ) {
    int status;
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p)", __func__, plugin, info, error );

    VALUE callback = osync_rubymodule_get_data ( plugin, "initialize_func" );

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[2];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( plugin ), SWIGTYPE_p_OSyncPlugin, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    VALUE result = rb_fcall2_protected ( callback, "call", 2, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call plugin initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    rb_gc_register_address(&result);
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: %lu", __func__, result );

    return ( void* ) result;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return;
}

// SGST: add some parameters in order to free sink datas
static void osync_rubymodule_plugin_finalize ( OSyncPlugin *plugin, void* plugin_data ) {
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, plugin );

    int status;
    VALUE callback = osync_rubymodule_get_data ( plugin, "finalize_func" );

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[2];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( plugin ), SWIGTYPE_p_OSyncPlugin, 0 |  0 );
    args[1] = CAST_VALUE(plugin_data);
    /*VALUE result = */
    rb_fcall2_protected ( callback, "call", 2, args, &status );
    /* there is no error return, no one is interested if finalize fails
    if (status!=0) {
      	osync_error_set(error, OSYNC_ERROR_INITIALIZATION, "Failed to call plugin finalize function!\n%s",
    	  osync_rubymodule_error_bt(__FILE__, __func__,__LINE__));
    goto error;
    } */
    if (plugin_data)
      rb_gc_unregister_address((VALUE*)&plugin_data);
    pthread_mutex_unlock ( &ruby_context_lock );

    osync_trace ( TRACE_EXIT, "%s", __func__ );
}

/* Here we actually tell opensync which sinks are available. For this plugin, we
 * just report all objtype as available. Since the resource are configured like this. */
static osync_bool osync_rubymodule_plugin_discover ( OSyncPlugin *plugin, OSyncPluginInfo *info, void* plugin_data, OSyncError **error ) {
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p)", __func__, plugin, info, error );

    VALUE callback = osync_rubymodule_get_data ( plugin, "discover_func" );
    int status;

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[3];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( plugin ), SWIGTYPE_p_OSyncPlugin, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[2] = CAST_VALUE(plugin_data);
    VALUE result = rb_fcall2_protected ( callback, "call", 3, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call plugin discover function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    pthread_mutex_unlock ( &ruby_context_lock );

    osync_trace ( TRACE_EXIT, "%s: %i", __func__, RBOOL(result) );
    return RBOOL(result);
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
// static pthread_mutex_t objformat_data_lock = PTHREAD_MUTEX_INITIALIZER;
// static OSyncList *objformats_data = NULL;
// typedef struct ObjectFormat2Data {
//     OSyncObjFormat     		*format;
//     OSyncRubyModuleObjectFormatData *data;
// } OSyncObjectFormat2Data;

// TODO
// static void free_objformat_data(OSyncRubyModuleObjectFormatData *data)
// {
//     // TODO: cleanup ruby objects
//     free(data);
// }

static void *osync_rubymodule_objformat_initialize ( OSyncObjFormat *format, OSyncError **error ) {
    int status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    VALUE callback = osync_rubymodule_get_data ( format, "initialize_func" );

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[1];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    VALUE result = rb_fcall2_protected ( callback, "call", 1, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call objformat initialize function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: %lu", __func__, result );
    rb_gc_register_address(&result);
    return ( void * ) result;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return NULL;
}

static osync_bool osync_rubymodule_objformat_finalize ( OSyncObjFormat *format, void *user_data, OSyncError **error ) {
    int status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    VALUE callback = osync_rubymodule_get_data ( format, "finalize_func" );

    pthread_mutex_lock ( &ruby_context_lock );

    VALUE args[1];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    VALUE result = rb_fcall2_protected ( callback, "call", 1, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call objformat finalize function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( !RBOOL ( result ) )
        goto error;

    // Should I need this (and ruby unref)?
    //format_data->data=NULL;
    if (user_data)
      rb_gc_unregister_address((VALUE*)&user_data);

    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: %i", __func__, TRUE );
    return TRUE;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}

static OSyncConvCmpResult osync_rubymodule_objformat_compare ( OSyncObjFormat *format, const char *leftdata, unsigned int leftsize, const char *rightdata, unsigned int rightsize, void *user_data, OSyncError **error ) {
    int status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );
    VALUE callback = osync_rubymodule_get_data ( format, "compare_func" );

    pthread_mutex_lock ( &ruby_context_lock );

    VALUE args[4];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    args[1] = SWIG_FromCharPtrAndSize ( leftdata, leftsize );
    args[2] = SWIG_FromCharPtrAndSize ( rightdata, rightsize );
    args[3] = CAST_VALUE(user_data);
    VALUE result = rb_fcall2_protected ( callback, "call", 4, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Failed to call objformat initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( !IS_FIXNUM ( result ) ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "The result should of compare should be a FixNum!\n" );
        goto error;
    }
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: %i", __func__, FIX2INT ( result ) );
    return FIX2INT ( result );
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return 0;
}

static char *osync_rubymodule_objformat_print ( OSyncObjFormat *format, const char *data, unsigned int size, void *user_data, OSyncError **error ) {
    int status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );
    VALUE callback = osync_rubymodule_get_data ( format, "print_func" );

    pthread_mutex_lock ( &ruby_context_lock );

    VALUE args[3];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    args[1] = SWIG_FromCharPtrAndSize ( data, size );
    args[2] = CAST_VALUE(user_data);
    VALUE result = rb_fcall2_protected ( callback, "call", 3, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call objformat initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( !IS_STRING ( result ) ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "The result should of print should be a String!\n" );
        goto error;
    }
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: %p", __func__, RSTRING_PTR ( result ) );
    return RSTRING_PTR ( result );
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return NULL;
}

static time_t osync_rubymodule_objformat_revision ( OSyncObjFormat *format, const char *data, unsigned int size, void *user_data, OSyncError **error ) {
    int status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );
    VALUE callback = osync_rubymodule_get_data ( format, "revision_func" );

    pthread_mutex_lock ( &ruby_context_lock );

    VALUE args[3];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    args[1] = SWIG_FromCharPtrAndSize ( data, size );
    args[2] = CAST_VALUE(user_data);
    VALUE result = rb_fcall2_protected ( callback, "call", 3, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Failed to call objformat initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }

    if ( !IS_TIME ( result ) ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "The result should of revision should be a Time!\n" );
        goto error;
    }

    result = rb_fcall2_protected ( result, "to_i", 0, NULL, &status );
    if ( ( status!=0 ) || !IS_FIXNUM ( result ) ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Failed to convert time to a number!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }

    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: %li", __func__,FIX2LONG ( result ) );
    return FIX2LONG ( result );
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return 0;
}

static osync_bool osync_rubymodule_objformat_destroy ( OSyncObjFormat *format, char *data, unsigned int size, void *user_data, OSyncError **error ) {
    int status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );
    VALUE callback = osync_rubymodule_get_data ( format, "destroy_func" );

    pthread_mutex_lock ( &ruby_context_lock );

    VALUE args[3];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    args[1] = SWIG_FromCharPtrAndSize ( data, size );
    args[2] = CAST_VALUE(user_data);
    VALUE result = rb_fcall2_protected ( callback, "call", 3, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call objformat initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( !RBOOL ( result ) )
        goto error;

    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: true", __func__ );
    return TRUE;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}

static osync_bool osync_rubymodule_objformat_copy ( OSyncObjFormat *format, const char *input, unsigned int insize, char **output, unsigned int *outpsize, void *user_data, OSyncError **error ) {
    int status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );
    VALUE callback = osync_rubymodule_get_data ( format, "copy_func" );

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[2];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    args[1] = SWIG_FromCharPtrAndSize ( input, insize );
    args[2] = CAST_VALUE(user_data);
    VALUE result = rb_fcall2_protected ( callback, "call", 3, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call objformat initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( !IS_STRING ( result ) ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "The result of copy should be a String!\n" );
        goto error;
    }

    *output = RSTRING_PTR ( result );

    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: true", __func__ );
    return TRUE;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}

static osync_bool osync_rubymodule_objformat_duplicate ( OSyncObjFormat *format, const char *uid, const char *input, unsigned int insize, char **newuid, char **output, unsigned int *outsize, osync_bool *dirty, void *user_data, OSyncError **error ) {
    int status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    VALUE callback = osync_rubymodule_get_data ( format, "duplicate_func" );

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[4];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    args[1] = SWIG_FromCharPtrAndSize ( uid, strlen ( uid ) );
    args[2] = SWIG_FromCharPtrAndSize ( input, insize );
    args[3] = CAST_VALUE(user_data);
    VALUE result = rb_fcall2_protected ( callback, "call", 4, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call objformat initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( !IS_ARRAY ( result ) || ( RARRAY_LEN ( result ) != 3 ) ||
            !IS_STRING ( rb_ary_entry ( result, 0 ) ) ||
            !IS_STRING ( rb_ary_entry ( result, 1 ) ) ||
            !IS_BOOL ( rb_ary_entry ( result, 2 ) )
       ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "The result should of print should be an Array with [newuid:string, output:string, dirty:bool] !\n" );
        goto error;
    }

    *newuid = RSTRING_PTR ( rb_ary_entry ( result, 0 ) );
    *output = RSTRING_PTR ( rb_ary_entry ( result, 1 ) );
    *dirty  = RBOOL ( rb_ary_entry ( result, 2 ) );

    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: true", __func__ );
    return TRUE;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}

// TODO
static osync_bool osync_rubymodule_objformat_create ( OSyncObjFormat *format, char **data, unsigned int *size, void *user_data, OSyncError **error ) {
    int status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    VALUE callback = osync_rubymodule_get_data ( format, "create_func" );

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[2];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    args[1] = CAST_VALUE(user_data);
    VALUE result = rb_fcall2_protected ( callback, "call", 2, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call objformat initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( !RBOOL ( result ) )
        goto error;

    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: true", __func__ );
    return TRUE;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}

// TODO
static osync_bool osync_rubymodule_objformat_marshal ( OSyncObjFormat *format, const char *input, unsigned int inpsize, OSyncMarshal *marshal, void *user_data, OSyncError **error ) {
    int status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    VALUE callback = osync_rubymodule_get_data ( format, "marshal_func" );

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[1];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    VALUE result = rb_fcall2_protected ( callback, "call", 1, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call objformat initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( !RBOOL ( result ) )
        goto error;

    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: true", __func__ );
    return TRUE;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}

// TODO
static osync_bool osync_rubymodule_objformat_demarshal ( OSyncObjFormat *format, OSyncMarshal *marshal, char **output, unsigned int *outpsize, void *user_data, OSyncError **error ) {
    int status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    VALUE callback = osync_rubymodule_get_data ( format, "demarshal_func" );

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[1];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    VALUE result = rb_fcall2_protected ( callback, "call", 1, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call objformat initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( !RBOOL ( result ) )
        goto error;

    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: true", __func__ );
    return TRUE;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}

// TODO
static osync_bool osync_rubymodule_objformat_validate ( OSyncObjFormat *format, const char *data, unsigned int size, void *user_data, OSyncError **error ) {
    int status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    VALUE callback = osync_rubymodule_get_data ( format, "validate_func" );

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[1];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    VALUE result = rb_fcall2_protected ( callback, "call", 1, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call objformat initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( !RBOOL ( result ) )
        goto error;

    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: true", __func__ );
    return TRUE;
error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}

void ruby_initialize();

osync_bool get_sync_info ( OSyncPluginEnv *env, OSyncError **error ) {
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

    if ( !RBOOL ( result ) ) {
        goto error;
    }

    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: true", __func__ );
    return TRUE;
error:
    osync_trace ( TRACE_ERROR, "Unable to register: %s", osync_error_print ( error ) );
    pthread_mutex_unlock ( &ruby_context_lock );
    return FALSE;
}

osync_bool get_format_info ( OSyncFormatEnv *env, OSyncError **error ) {
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

    if ( !RBOOL ( result ) ) {
        goto error;
    }

    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: true", __func__ );
    return TRUE;
error:
    osync_trace ( TRACE_ERROR, "Unable to register: %s", osync_error_print ( error ) );
    pthread_mutex_unlock ( &ruby_context_lock );
    return FALSE;
}

osync_bool get_conversion_info ( OSyncFormatEnv *env, OSyncError **error ) {
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
    VALUE result = rb_fcall2_protected ( plugin_class,"get_conversion_info",1, args,&status );
    if ( status!=0 ) {
        fprintf ( stderr, "%s\n", osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Failed to call %s.get_conversion_info!\n%s", RUBY_FORMAT_CLASS,
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }

    if ( !RBOOL ( result ) ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "%s.get_conversion_info returned false or nil (which means problems)!\n%s", RUBY_FORMAT_CLASS,
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }

    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: true", __func__ );
    return TRUE;
error:
    osync_trace ( TRACE_ERROR, "Unable to register: %s", osync_error_print ( error ) );
    pthread_mutex_unlock ( &ruby_context_lock );
    return FALSE;
}


/** Plugin */

// VALUE
// rb_osync_plugin_get_data ( int argc, VALUE *argv, VALUE self ) {
//     OSyncPlugin *arg1 = ( OSyncPlugin * ) 0 ;
//     void *argp1 = 0 ;
//     int res1 = 0 ;
//     VALUE vresult = Qnil;
//
//     if ( ( argc < 1 ) || ( argc > 1 ) ) {
//         rb_raise ( rb_eArgError, "wrong # of arguments(%d for 1)",argc );
//         SWIG_fail;
//     }
//     res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncPlugin, 0 |  0 );
//     if ( !SWIG_IsOK ( res1 ) ) {
//         SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncPlugin *","osync_plugin_get_data", 1, argv[0] ) );
//     }
//     arg1 = ( OSyncPlugin * ) ( argp1 );
//     vresult = ( VALUE ) osync_plugin_get_data ( arg1 );
//     if ( !vresult )
//         vresult=Qnil;
//     return vresult;
// fail:
//     return Qnil;
// }

VALUE
rb_osync_plugin_set_data ( int argc, VALUE *argv, VALUE self ) {
    OSyncPlugin *arg1 = ( OSyncPlugin * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;
    VALUE data = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncPlugin, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncPlugin *","osync_plugin_set_data", 1, argv[0] ) );
    }
    arg1 = ( OSyncPlugin * ) ( argp1 );

    data = ( VALUE ) osync_plugin_get_data ( arg1 );
    if ( data )
        rb_gc_unregister_address ( &data );

    osync_plugin_set_data ( arg1, ( void* ) data );
    rb_gc_register_address ( &data );

    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_plugin_set_initialize_func ( int argc, VALUE *argv, VALUE self ) {
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

    osync_rubymodule_set_data ( argp1, "initialize_func", argv[1] );

    osync_plugin_set_initialize_func ( arg1, osync_rubymodule_plugin_initialize );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_plugin_set_finalize_func ( int argc, VALUE *argv, VALUE self ) {
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
    osync_rubymodule_set_data ( argp1, "finalize_func", argv[1] );
    osync_plugin_set_finalize_func ( arg1,osync_rubymodule_plugin_finalize );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_plugin_set_discover_func ( int argc, VALUE *argv, VALUE self ) {
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
    osync_rubymodule_set_data ( argp1, "discover_func", argv[1] );
    osync_plugin_set_discover_func ( arg1,osync_rubymodule_plugin_discover );
    return Qnil;
fail:
    return Qnil;
}


/** ObjectType Sinks */

VALUE
rb_osync_rubymodule_objtype_sink_get_userdata ( int argc, VALUE *argv, VALUE self ) {
    OSyncObjTypeSink *arg1 = ( OSyncObjTypeSink * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;
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
    vresult = ( VALUE ) osync_objtype_sink_get_userdata ( arg1 );

    if ( !vresult )
        vresult=Qnil;
    return vresult;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objtype_sink_set_userdata ( int argc, VALUE *argv, VALUE self ) {
    OSyncObjTypeSink *arg1 = ( OSyncObjTypeSink * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;
    VALUE data;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjTypeSink *","osync_objtype_sink_set_data", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjTypeSink * ) ( argp1 );
    data = ( VALUE ) osync_objtype_sink_get_userdata ( arg1 );
    if ( data )
        rb_gc_unregister_address ( &data );

    osync_objtype_sink_set_userdata ( arg1, ( void* ) argv[1] );
    rb_gc_register_address ( &data );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objtype_sink_set_read_func ( int argc, VALUE *argv, VALUE self ) {
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
    osync_rubymodule_set_data ( argp1, "read_func", argv[1] );
    osync_objtype_sink_set_read_func ( arg1,osync_rubymodule_objtype_sink_read );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objtype_sink_set_sync_done_func ( int argc, VALUE *argv, VALUE self ) {
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
    osync_rubymodule_set_data ( argp1, "sync_done_func", argv[1] );
    osync_objtype_sink_set_sync_done_func ( arg1,osync_rubymodule_objtype_sink_sync_done );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objtype_sink_set_connect_done_func ( int argc, VALUE *argv, VALUE self ) {
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
    osync_rubymodule_set_data ( argp1, "connect_done_func", argv[1] );
    osync_objtype_sink_set_connect_done_func ( arg1,osync_rubymodule_objtype_sink_connect_done );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objtype_sink_set_disconnect_func ( int argc, VALUE *argv, VALUE self ) {
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
    osync_rubymodule_set_data ( argp1, "disconnect_func", argv[1] );
    osync_objtype_sink_set_disconnect_func ( arg1,osync_rubymodule_objtype_sink_disconnect );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objtype_sink_set_connect_func ( int argc, VALUE *argv, VALUE self ) {
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
    osync_rubymodule_set_data ( argp1, "connect_func", argv[1] );
    osync_objtype_sink_set_connect_func ( arg1, osync_rubymodule_objtype_sink_connect );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objtype_sink_set_get_changes_func ( int argc, VALUE *argv, VALUE self ) {
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
    osync_rubymodule_set_data ( argp1, "get_changes_func", argv[1] );
    osync_objtype_sink_set_get_changes_func ( arg1, osync_rubymodule_objtype_sink_get_changes );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objtype_sink_set_commit_func ( int argc, VALUE *argv, VALUE self ) {
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
    osync_rubymodule_set_data ( argp1, "commit_func", argv[1] );
    osync_objtype_sink_set_commit_func ( arg1,osync_rubymodule_objtype_sink_commit );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objtype_sink_set_committed_all_func ( int argc, VALUE *argv, VALUE self ) {
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
    osync_rubymodule_set_data ( argp1, "commited_all_func", argv[1] );
    osync_objtype_sink_set_committed_all_func ( arg1,osync_rubymodule_objtype_sink_commited_all );
    return Qnil;
fail:
    return Qnil;
}

/** ObjectFormat */

// VALUE
// rb_osync_objformat_get_data ( int argc, VALUE *argv, VALUE self )
// {
//     OSyncObjFormat *arg1 = ( OSyncObjFormat * ) 0 ;
//     void *argp1 = 0 ;
//     int res1 = 0 ;
//     VALUE vresult = Qnil;
//
//     if ( ( argc < 1 ) || ( argc > 1 ) ) {
//         rb_raise ( rb_eArgError, "wrong # of arguments(%d for 1)",argc );
//         SWIG_fail;
//     }
//     res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
//     if ( !SWIG_IsOK ( res1 ) ) {
//         SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjFormat *","osync_objformat_get_data", 1, argv[0] ) );
//     }
//     arg1 = ( OSyncObjFormat * ) ( argp1 );
//     vresult = (VALUE) osync_objformat_get_data( arg1 );
//     if ( !vresult )
//         vresult=Qnil;
//     return vresult;
// fail:
//     return Qnil;
// }

// VALUE
// rb_osync_objformat_set_data ( int argc, VALUE *argv, VALUE self )
// {
//     OSyncObjFormat *arg1 = ( OSyncObjFormat * ) 0 ;
//     void *argp1 = 0 ;
//     int res1 = 0 ;
//     VALUE data;
//
//     if ( ( argc < 2 ) || ( argc > 2 ) ) {
//         rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
//         SWIG_fail;
//     }
//     res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
//     if ( !SWIG_IsOK ( res1 ) ) {
//         SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjFormat *","osync_objformat_set_data", 1, argv[0] ) );
//     }
//     arg1 = ( OSyncObjFormat * ) ( argp1 );
//
//     data = ( VALUE ) osync_objformat_get_data ( arg1 );
//     if ( data )
//         rb_gc_unregister_address ( &data );
//
//     osync_plugin_set_data(data);
//     rb_gc_register_address ( &data );
//     return Qnil;
// fail:
//     return Qnil;
// }

VALUE
rb_osync_rubymodule_objformat_set_initialize_func ( int argc, VALUE *argv, VALUE self ) {
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
    osync_rubymodule_set_data ( argp1, "initialize_func", argv[1] );
    osync_objformat_set_initialize_func ( arg1, osync_rubymodule_objformat_initialize );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objformat_set_finalize_func ( int argc, VALUE *argv, VALUE self ) {
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
    osync_rubymodule_set_data ( argp1, "finalize_func", argv[1] );
    osync_objformat_set_finalize_func ( arg1, osync_rubymodule_objformat_finalize );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objformat_set_compare_func ( int argc, VALUE *argv, VALUE self ) {
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
    osync_rubymodule_set_data ( argp1, "compare_func", argv[1] );
    osync_objformat_set_compare_func ( arg1, osync_rubymodule_objformat_compare );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objformat_set_destroy_func ( int argc, VALUE *argv, VALUE self ) {
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
    osync_rubymodule_set_data ( argp1, "destroy_func", argv[1] );
    osync_objformat_set_destroy_func ( arg1, osync_rubymodule_objformat_destroy );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objformat_set_copy_func ( int argc, VALUE *argv, VALUE self ) {
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
    osync_rubymodule_set_data ( argp1, "copy_func", argv[1] );
    osync_objformat_set_copy_func ( arg1, osync_rubymodule_objformat_copy );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objformat_set_duplicate_func ( int argc, VALUE *argv, VALUE self ) {
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
    osync_rubymodule_set_data ( argp1, "duplicate_func", argv[1] );
    osync_objformat_set_duplicate_func ( arg1, osync_rubymodule_objformat_duplicate );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objformat_set_create_func ( int argc, VALUE *argv, VALUE self ) {
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
    osync_rubymodule_set_data ( argp1, "create_func", argv[1] );
    osync_objformat_set_create_func ( arg1, osync_rubymodule_objformat_create );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objformat_set_print_func ( int argc, VALUE *argv, VALUE self ) {
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
    osync_rubymodule_set_data ( argp1, "print_func", argv[1] );
    osync_objformat_set_print_func ( arg1, osync_rubymodule_objformat_print );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objformat_set_revision_func ( int argc, VALUE *argv, VALUE self ) {
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
    osync_rubymodule_set_data ( argp1, "revision_func", argv[1] );
    osync_objformat_set_revision_func ( arg1, osync_rubymodule_objformat_revision );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objformat_set_marshal_func ( int argc, VALUE *argv, VALUE self ) {
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
    osync_rubymodule_set_data ( argp1, "mashal_func", argv[1] );
    osync_objformat_set_marshal_func ( arg1, osync_rubymodule_objformat_marshal );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objformat_set_demarshal_func ( int argc, VALUE *argv, VALUE self ) {
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
    osync_rubymodule_set_data ( argp1, "demarshal_func", argv[1] );
    osync_objformat_set_demarshal_func ( arg1, osync_rubymodule_objformat_demarshal );
    return Qnil;
fail:
    return Qnil;
}

VALUE
rb_osync_rubymodule_objformat_set_validate_func ( int argc, VALUE *argv, VALUE self ) {
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
    osync_rubymodule_set_data ( argp1, "validate_func", argv[1] );
    osync_objformat_set_validate_func ( arg1, osync_rubymodule_objformat_validate );
    return Qnil;
fail:
    return Qnil;
}


/* Converter */

// TODO: Need caps reference in callback
static osync_bool osync_rubymodule_converter_convert ( char *input, unsigned int inpsize, char **output, unsigned int *outpsize, osync_bool *free_input, const char *config, void *user_data, OSyncError **error ) {
    /*int status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    return FALSE;

    VALUE callback = osync_rubymodule_get_data ( format, "validate_func");

    pthread_mutex_lock ( &ruby_context_lock );
    VALUE args[1];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    VALUE result = rb_fcall2_protected ( callback, "call", 1, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call objformat initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if (!RBOOL(result))
        goto error;

    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT, "%s: %p", __func__, result );
    return TRUE;
    error:
    pthread_mutex_unlock ( &ruby_context_lock );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;*/
}



/*
  Document-method: Opensync.osync_converter_new

  call-seq:
    osync_converter_new(OSyncConverterType type, OSyncObjFormat sourceformat,
    OSyncObjFormat targetformat, OSyncFormatConvertFunc convert_func,
    OSyncError error) -> OSyncFormatConverter

A module function.

*/
SWIGINTERN VALUE
rb_osync_converter_new ( int argc, VALUE *argv, VALUE self ) {
    OSyncConverterType arg1 ;
    OSyncObjFormat *arg2 = ( OSyncObjFormat * ) 0 ;
    OSyncObjFormat *arg3 = ( OSyncObjFormat * ) 0 ;
    OSyncFormatConvertFunc arg4 = ( OSyncFormatConvertFunc ) 0 ;
    OSyncError **arg5 = ( OSyncError ** ) 0 ;
    OSyncError *error5 ;
    int val1 ;
    int ecode1 = 0 ;
    void *argp2 = 0 ;
    int res2 = 0 ;
    void *argp3 = 0 ;
    int res3 = 0 ;
    void *argp5 = 0 ;
    int res5 = 0 ;
    OSyncFormatConverter *result = 0 ;
    VALUE vresult = Qnil;

    {
        error5 = NULL;
        arg5 = &error5;
    }
    if ( ( argc < 4 ) || ( argc > 5 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 4)",argc );
        SWIG_fail;
    }
    ecode1 = SWIG_AsVal_int ( argv[0], &val1 );
    if ( !SWIG_IsOK ( ecode1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( ecode1 ), Ruby_Format_TypeError ( "", "OSyncConverterType","osync_converter_new", 1, argv[0] ) );
    }
    arg1 = ( OSyncConverterType ) ( val1 );
    res2 = SWIG_ConvertPtr ( argv[1], &argp2,SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    if ( !SWIG_IsOK ( res2 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res2 ), Ruby_Format_TypeError ( "", "OSyncObjFormat *","osync_converter_new", 2, argv[1] ) );
    }
    arg2 = ( OSyncObjFormat * ) ( argp2 );
    res3 = SWIG_ConvertPtr ( argv[2], &argp3,SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    if ( !SWIG_IsOK ( res3 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res3 ), Ruby_Format_TypeError ( "", "OSyncObjFormat *","osync_converter_new", 3, argv[2] ) );
    }
    arg3 = ( OSyncObjFormat * ) ( argp3 );
    {
        //TODO: Check if it answer "call"
//     int res = SWIG_ConvertFunctionPtr(argv[3], (void**)(&arg4), SWIGTYPE_p_f_p_char_unsigned_int_p_p_char_p_unsigned_int_p_int_p_q_const__char_p_void_p_p_struct_OSyncError__int);
//     if (!SWIG_IsOK(res)) {
//       SWIG_exception_fail(SWIG_ArgError(res), Ruby_Format_TypeError( "", "OSyncFormatConvertFunc","osync_converter_new", 4, argv[3] ));
//     }
    }
    if ( argc > 4 ) {
        res5 = SWIG_ConvertPtr ( argv[4], &argp5,SWIGTYPE_p_p_OSyncError, 0 |  0 );
        if ( !SWIG_IsOK ( res5 ) ) {
            SWIG_exception_fail ( SWIG_ArgError ( res5 ), Ruby_Format_TypeError ( "", "OSyncError **","osync_converter_new", 5, argv[4] ) );
        }
        arg5 = ( OSyncError ** ) ( argp5 );
    }
    result = ( OSyncFormatConverter * ) osync_converter_new ( arg1,arg2,arg3,osync_rubymodule_converter_convert,arg5 );
    osync_rubymodule_set_data ( result, "convert_func", argv[3] );
    vresult = SWIG_NewPointerObj ( SWIG_as_voidptr ( result ), SWIGTYPE_p_OSyncFormatConverter, 0 |  0 );
    {
        if ( error5 ) {
            /*     VALUE rb_eOSync = rb_path2class("Opensync::Error");
            *     if (rb_eOSync == Qnil)
            *       rb_eOSync = rb_eStandardError;  */
            rb_raise ( rb_eStandardError, "%s",osync_error_print ( &error5 ) );
            osync_error_unref ( &error5 );
            SWIG_fail;
        }
    }
    return vresult;
fail: {
        if ( error5 ) {
            /*     VALUE rb_eOSync = rb_path2class("Opensync::Error");
            *     if (rb_eOSync == Qnil)
            *       rb_eOSync = rb_eStandardError;  */
            rb_raise ( rb_eStandardError, "%s",osync_error_print ( &error5 ) );
            osync_error_unref ( &error5 );
            SWIG_fail;
        }
    }
    return Qnil;
}


/*
  Document-method: Opensync.osync_caps_converter_new

  call-seq:
    osync_caps_converter_new(char sourceformat, char targetformat, OSyncCapsConvertFunc convert_func,
    OSyncError error) -> OSyncCapsConverter

A module function.

*/
// SWIGINTERN VALUE
// rb_osync_caps_converter_new(int argc, VALUE *argv, VALUE self) {
//   char *arg1 = (char *) 0 ;
//   char *arg2 = (char *) 0 ;
//   OSyncCapsConvertFunc arg3 = (OSyncCapsConvertFunc) 0 ;
//   OSyncError **arg4 = (OSyncError **) 0 ;
//   OSyncError *error4 ;
//   int res1 ;
//   char *buf1 = 0 ;
//   int alloc1 = 0 ;
//   int res2 ;
//   char *buf2 = 0 ;
//   int alloc2 = 0 ;
//   void *argp4 = 0 ;
//   int res4 = 0 ;
//   OSyncCapsConverter *result = 0 ;
//   VALUE vresult = Qnil;
//
//   {
//     error4 = NULL;
//     arg4 = &error4;
//   }
//   if ((argc < 3) || (argc > 4)) {
//     rb_raise(rb_eArgError, "wrong # of arguments(%d for 3)",argc); SWIG_fail;
//   }
//   res1 = SWIG_AsCharPtrAndSize(argv[0], &buf1, NULL, &alloc1);
//   if (!SWIG_IsOK(res1)) {
//     SWIG_exception_fail(SWIG_ArgError(res1), Ruby_Format_TypeError( "", "char const *","osync_caps_converter_new", 1, argv[0] ));
//   }
//   arg1 = (char *)(buf1);
//   res2 = SWIG_AsCharPtrAndSize(argv[1], &buf2, NULL, &alloc2);
//   if (!SWIG_IsOK(res2)) {
//     SWIG_exception_fail(SWIG_ArgError(res2), Ruby_Format_TypeError( "", "char const *","osync_caps_converter_new", 2, argv[1] ));
//   }
//   arg2 = (char *)(buf2);
//
//
//   {
//       //TODO: Check if it
// //     int res = SWIG_ConvertFunctionPtr(argv[2], (void**)(&arg3), SWIGTYPE_p_f_p_struct_OSyncCapabilities_p_p_struct_OSyncCapabilities_p_q_const__char_p_void_p_p_struct_OSyncError__int);
// //     if (!SWIG_IsOK(res)) {
// //       SWIG_exception_fail(SWIG_ArgError(res), Ruby_Format_TypeError( "", "OSyncCapsConvertFunc","osync_caps_converter_new", 3, argv[2] ));
// //     }
//   }
//
//   if (argc > 3) {
//     res4 = SWIG_ConvertPtr(argv[3], &argp4,SWIGTYPE_p_p_OSyncError, 0 |  0 );
//     if (!SWIG_IsOK(res4)) {
//       SWIG_exception_fail(SWIG_ArgError(res4), Ruby_Format_TypeError( "", "OSyncError **","osync_caps_converter_new", 4, argv[3] ));
//     }
//     arg4 = (OSyncError **)(argp4);
//   }
//
//   result = (OSyncCapsConverter *)osync_caps_converter_new((char const *)arg1,(char const *)arg2,osync_rubymodule_caps_converter_convert,arg4);
//   osync_rubymodule_set_data(result, "convert_func", argv[2]);
//
//   vresult = SWIG_NewPointerObj(SWIG_as_voidptr(result), SWIGTYPE_p_OSyncCapsConverter, 0 |  0 );
//   if (alloc1 == SWIG_NEWOBJ) free((char*)buf1);
//   if (alloc2 == SWIG_NEWOBJ) free((char*)buf2);
//   {
//     if (error4) {
//       /*     VALUE rb_eOSync = rb_path2class("Opensync::Error");
//       *     if (rb_eOSync == Qnil)
//       *       rb_eOSync = rb_eStandardError;  */
//       rb_raise(rb_eStandardError, "%s",osync_error_print(&error4));
//       osync_error_unref(&error4);
//       SWIG_fail;
//     }
//   }
//   return vresult;
// fail:
//   if (alloc1 == SWIG_NEWOBJ) free((char*)buf1);
//   if (alloc2 == SWIG_NEWOBJ) free((char*)buf2);
//   {
//     if (error4) {
//       /*     VALUE rb_eOSync = rb_path2class("Opensync::Error");
//       *     if (rb_eOSync == Qnil)
//       *       rb_eOSync = rb_eStandardError;  */
//       rb_raise(rb_eStandardError, "%s",osync_error_print(&error4));
//       osync_error_unref(&error4);
//       SWIG_fail;
//     }
//   }
//   return Qnil;
// }

void ruby_initialize() {
    if ( ruby_initialized ) {
        return;
    }

    ruby_initialized=TRUE;

    /* Initialize Ruby env */
    RUBY_INIT_STACK;
    ruby_init();
    ruby_init_loadpath();
    ruby_script ( RUBY_SCRIPTNAME );
    // SWIG initialize (include the module)
    Init_opensync();

    // Include custom made methods (should it be inside opensync.i? I don't think so)
    //rb_define_module_function ( mOpensync, "get_data", rb_osync_rubymodule_get_data, -1 );
    //rb_define_module_function ( mOpensync, "set_data", rb_osync_rubymodule_set_data, -1 );

    // Those replace set/get_*data and set_*_func
    rb_define_module_function ( mOpensync, "osync_rubymodule_get_data", rb_osync_rubymodule_get_data, -1 );
    rb_define_module_function ( mOpensync, "osync_rubymodule_set_data", rb_osync_rubymodule_set_data, -1 );
    rb_define_module_function ( mOpensync, "osync_rubymodule_clean_data", rb_osync_rubymodule_clean_data, -1 );

    rb_define_module_function ( mOpensync, "osync_plugin_set_initialize_func", rb_osync_plugin_set_initialize_func, -1 );
    rb_define_module_function ( mOpensync, "osync_plugin_set_finalize_func", rb_osync_plugin_set_finalize_func, -1 );
    rb_define_module_function ( mOpensync, "osync_plugin_set_discover_func", rb_osync_plugin_set_discover_func, -1 );

    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_userdata", rb_osync_rubymodule_objtype_sink_set_userdata, -1 );
    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_connect_func", rb_osync_rubymodule_objtype_sink_set_connect_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_get_changes_func", rb_osync_rubymodule_objtype_sink_set_get_changes_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_commit_func", rb_osync_rubymodule_objtype_sink_set_commit_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_committed_all_func", rb_osync_rubymodule_objtype_sink_set_committed_all_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_read_func", rb_osync_rubymodule_objtype_sink_set_read_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_sync_done_func", rb_osync_rubymodule_objtype_sink_set_sync_done_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_connect_done_func", rb_osync_rubymodule_objtype_sink_set_connect_done_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_disconnect_func", rb_osync_rubymodule_objtype_sink_set_disconnect_func, -1 );

//     rb_define_module_function ( mOpensync, "osync_objformat_get_data",  rb_osync_objformat_get_data, -1 );
//     rb_define_module_function ( mOpensync, "osync_objformat_set_data" , rb_osync_objformat_set_data, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_set_initialize_func", rb_osync_rubymodule_objformat_set_initialize_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_set_finalize_func", rb_osync_rubymodule_objformat_set_finalize_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_set_compare_func", rb_osync_rubymodule_objformat_set_compare_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_set_destroy_func", rb_osync_rubymodule_objformat_set_destroy_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_set_copy_func", rb_osync_rubymodule_objformat_set_copy_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_set_duplicate_func", rb_osync_rubymodule_objformat_set_duplicate_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_set_create_func", rb_osync_rubymodule_objformat_set_create_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_set_print_func", rb_osync_rubymodule_objformat_set_print_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_set_revision_func", rb_osync_rubymodule_objformat_set_revision_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_set_marshal_func", rb_osync_rubymodule_objformat_set_marshal_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_set_demarshal_func", rb_osync_rubymodule_objformat_set_demarshal_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_set_validate_func", rb_osync_rubymodule_objformat_set_validate_func, -1 );

    rb_define_module_function ( mOpensync, "osync_converter_new", rb_osync_converter_new, -1 );

    rb_define_const(mOpensync, "OPENSYNC_RUBYPLUGIN_DIR", SWIG_FromCharPtr (OPENSYNC_RUBYPLUGIN_DIR));
    rb_define_const(mOpensync, "OPENSYNC_RUBYFORMATS_DIR", SWIG_FromCharPtr (OPENSYNC_RUBYFORMATS_DIR));
    rubymodule_data = g_hash_table_new_full ( g_direct_hash, g_direct_equal, NULL, ( GDestroyNotify ) g_hash_table_destroy );
}

void ruby_finalized() {
    g_hash_table_destroy ( rubymodule_data );

}

int get_version ( void ) {
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

// osync_caps_converter_new and osync_converter_new ????
// file-sync does not "osync_trace ( TRACE_EXIT, "%s: true", __func__);"
