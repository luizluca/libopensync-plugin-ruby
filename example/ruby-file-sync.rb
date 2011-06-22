#
# RUBY BIDINDS
#
# All opensync methods live inside module Opensync. However, those methods where
# mapped into ruby classes in a more ruby-way of programming. Indeed, they are still
# accessable and can be directly called.
#
#
# Almost all opensync methods are inside OSyncObject class or subclasses.
# Each instance has a property named _self that keeps its relative SWIG object. The
# SWIG object wraps C pointers represents that points to C Structure
#
#
# All arguments, properties or variable named as _xxx represents SWIG object.
# OSyncObject.map_objects can convert it to its relative ruby class and
# aClassInstace._self returns the SWIG Object
#
#
# OsyncObject is a superclass of all osync struct relative classes.
# Almost all osync methods are mapped into OSyncObject subclasses. The class name is
# derived from the opensync method name. I.E.:
#
# osync_plugin_*     maps to Opensync::Plugin class and instance methods
# osync_plugin_env_* maps to Opensync::Plugin::Env class and instance methods
#
#
# Opensync methods are mapped into ruby classes according to these rules:
#
# Class methods
# ..._new   to Myclass.alloc()
# ..._unref to Myclass.unref(ref)
#
# Instance methods:
# ...get_xxx(...) to xxx()
# ...set_xxx(...) to xxx=(value)
#  ...is_xxx(...) to xxx?()
#
# All remaining methods are directly mapped
# ...find_xxx(...) ti find_xxx(...)
#
#
# OSyncObject class reimplements new method to alloc a new C struct.
# When MyClass.new is called, it allocs the C struct (Myclass.alloc) and also sets
# a trigger to call unref the C structure when GC clean the object reference.
#
#
#   plugin = Opensync::Plugin.new
#   plugin.name = "example"
#
# Is equivalent to C code:
#
#  OSyncPlugin plugin = osync_plugin_new(&error);
#  osync_plugin_set_name(plugin, "example");
#
# A new Ruby Class instance can be obtained from a SWIG object by using the Myclass.from method:
#
#   env = Opensync::Plugin::Env.from(_env)
#
# Or the class can be selected automaticly using Opensync::OsyncObject.map_object(_xxx)
#
#  env = Opensync::OsyncObject.map_object(_env)
#
#
# ARGUMENTS AND RETURNS
#
# Basic types like numbers, boolean, string, are converted automaticly to and
# from C/Ruby world by SWIG. No need to care about them.
#
# When a OsyncList is returned from a function, it is converted into a Ruby Array with the
# list items converted into relative ruby class instance
#
# All methods that has "OSyncError** error" mark it as optional. If error parameters is not
# defined, SWIG wrappers deal with it and if it returns something (error* != NULL), it raises an
# Ruby exception with its message.
#
#
# CALLBACKS
#
# Methods that defines callbacks are reimplemented to work with ruby world. In ruby, they can be defined
# with any callable object:
#
#  Opensync.osync_plugin_set_discovery_fn=Proc.new {|_arg1, _arg2| do something }
#
# Or under OsyncObject classes:
#
#  plugin.discover=Proc.new {|_info, _data| info=Info.for(_info); info.xxx }
#
# The received arguments are SWIG objects. However, OsyncObject.callback creates a callable block that
# converts SWIG to/from ruby.
#
#  plugin.discover=callback {|info, data| info.xxx }
#
#
# TRACES
#
# rubymodule intercepts calls to methods and send the appropriated osync_trace message
#

require "pathname"

class RubyFileSync < Opensync::Plugin
      class Dir
	  attr_accessor :env, :sink, :recursive, :path
      end

      class FileSyncEnv
	  attr_reader :directories
	  def initialize
	      @directories=[]
	  end
      end

      def initialize_new
	  self.name="ruby-file-sync"
	  self.longname="File Synchronization Plugin"
	  self.description="Plugin to synchronize files on the local filesystem"
	  self.initialize_func=self.callback {|plugin, info| initialize0(info)}
	  self.finalize_func=self.callback   {|plugin, plugin_data| finalize(plugin_data) }
	  self.discover_func=self.callback   {|plugin, info| discover(info)}
      end

      def initialize0(info)
	  env=FileSyncEnv.new
	  config = info.config

	  info.objtype_sinks.each do
	    |sink|
	    # Save plugin data into Dir
	    dir=Dir.new
	    dir.env=self.data
	    dir.sink=sink

	    objtype=sink.name
	    res = config.find_active_resource(objtype)
	    dir.path = res.path
	    pathes = []
	    if not dir.path or dir.path.empty?
		raise Opensync::OsyncMisconfigurationError.new("Path for object type \"#{objtype}\" is not configured.")
	    end
	    if pathes.include?(dir.path)
		raise Opensync::OsyncMisconfigurationError.new("Path for object type \"#{objtype}\" defined for more than one objtype.")
	    end
	    pathes << dir.path

	    res.objformat_sinks.each do
		|sink|
		objformat = sink.objformat

		# TODO: Implement objformat sanity check in OpenSync Core
		if objformat != "file"
		    raise Opensync::OsyncMisconfigurationError.new("Format \"#{objformat}\" is not supported by file-sync. Only Format \"file\" is currently supported by the file-sync plugin.")
		end
	    end

	    sink.connect_func=callback{|*args| connect_func(*args) }
	    sink.get_changes_func=callback{|*args| get_changes_func(*args) }
	    sink.commit_func=callback{|*args| commit_func(*args) }
	    sink.read_func=callback{|*args| read_func(*args) }
	    sink.sync_done_func=callback{|*args| sync_done(*args) }

	    sink.userdata=dir
	    sink.enable_state_db(true)
	    sink.enable_hashtable(true)

	    # This currently, 0.39.0 does not happen in C version of file-sync plugin
	    env.directories << dir
	  end
	  return env
      end

      def finalize(plugin_data)
	  # Clean the callback references (and let GC clean) TODO: check
	  plugin_data.directories.each {|dir| dir.sink.clean }

	  # Maybe a module can be initialized and finalized and initialized and finalize and...
	  # better don't save this bits
	  #self.clean
      end

      def discovery(info, plugin_data)
	  self.sinks.each {|sink| sink.avaiable=true}
	  version = OsyncVersion.new
	  version.plugin = self.name
	  info.version = version
      end

      def connect_func(sink, info, ctx, userdata)
	  dir = userdata
	  state_db = sink.state_db
	  pathmatch = nil

	  pathmatch = state_db.equal("path", dir.path)

	  ctx.report_slowsync if not pathmatch

	  if not File.directory?(dir.path)
	    raise Opensync::OSyncError.new("\"#{dir.path}\" is not a directory")
	  end

	  ctx.report_success
      end

      def read_func(sink, info, ctx, change, userdata)
	  dir=userdata
	  formatenv=info.format_env
	  tmp = filename_scape_characters(change.uid)
	  filename = Pathname.new(dir.path) + tmp

	  file = File.new(filename)
	  data = file.gets(nil)
	  odata = nil

	  file = FileFormat::FileFormatData.new
	  stat = file.stat
	  file.userid = stat.uid
	  file.groupid = stat.gid;
	  file.mode = stat.mode;
	  file.last_mod = stat.mtime;
	  file.data = data;
	  file.size = data.size;
	  file.path = change.uid;

	  fileformat = formatenv.find_objformat("file")
	  odata = Opensync::Data.new(file, fileformat)
	  odata.objtype=sink.name
	  change.data=odata
	  ctx.report_change(change)
      end

      def write(sink, info, ctx, change, userdata)

	  dir=userdata
	  tmp = filename_scape_characters(change.uid)
	  filename = Pathname.new(dir.path) + tmp

	  case change.changetype
	  when Opensync::OSYNC_CHANGE_TYPE_DELETED
	      File.unlink(filename)
	  when Opensync::OSYNC_CHANGE_TYPE_ADDED, Opensync::OSYNC_CHANGE_TYPE_MODIFIED
	      case change.changetype
	      when Opensync::OSYNC_CHANGE_TYPE_ADDED
		  if File.exist?(filename)
		      change.uid="#{change.uid}-new"
		      write(sink, info, ctx, change, userdata)
		  end
	      end

	      #FIXME add ownership for file-sync
	      odata = change.data
	      buffer = odata.data
	      file = FileFormatData.from_buf(buffer)

	      # Is my implementation equal?
	      # ??? osync_file_write (filename, file.data, file.size, file.mode)
	      File.open(filename, "w") {|io|
		  io.print(file.data)
	          io.chmod(file.mode)
	      }
	end
	return TRUE
      end

      def report_dir(dir, info, subdir, ctx)
	  formatenv=info.format_env
	  hashtable=dir.sink.hashtable

	  path = Pathname.new(dir.path) + subdir
	  path.entries do
	    |entry|
	    filename = Pathname.new(dir.path) + entry
	    if !subdir
		relative_filename = entr
	    else
		relative_filename = Pathname.new(subdir) + entry
	    end

	    if File.directory?(filename)
		if (dir.recursive?)
		    report_dir(dir, info, relative_filename, ctx)
		end
	    elsif File.file?(filename)
		change=Opensync::Change.new
		change.uid=relative_filename
		change.hash=generate_hash(File.new(filename))
		type = hashtable.changetype(change)
		change.changetype=type

		hashtable.update_change(change)

		next if type == Opensync::OSYNC_CHANGE_TYPE_UNMODIFIED

		# TODO: test this part
		# FIXME: use something that closes the file
		data = File.new(filename).gets(nil)
		odata = nil

		file = FileFormatData.new
		file.data = data;
		file.size = size;
		file.path = relative_filename;
		fileformat = formatenv.find_objformat("file")

		odata = Opensync::Data.new(file.to_buf, fileformat)
		odata.objtype=sink.name
		change.data=odata
		ctx.report_change(change)
	    end
	  end
      end

      def get_changes_func(sink, info, ctx, slow_sync, userdata)

	  dir=userdata
	  format_env = info.format_env
	  hashtable = sink.hashtable

	  hashtable.slowsync if (slow_sync)

	  report_dir(dir, info, "", ctx)

	  hashtable.deleted.each do
	      |uid|
	      change = Opensync::Change.new
	      change.uid=uid
	      change.changetype = Opensync::OSYNC_CHANGE_TYPE_DELETED

	      fileformat = format_env.find_objformat("file")
	      odata = Opensync::Data.new("", fileformat)
	      odata.objtype=sink.name
	      change.data=odata
	      ctx.report_change(change)
	      hashtable.update_change(change)
	end
	ctx.report_success
      end

      def filename_scape_characters(tmp)
	  tmp.tr('/!?:*\><@',"_")
      end

      def generate_hash(file)
	  "#{file.mtime.to_i}-#{file.ctime.to_i}"
      end

      def commit_func(sink, info, ctx, change, userdata)
	  dir=userdata
	  hashtable=sink.hashtable

	  write(sink, info, ctx, change, userdata)
	  filename="#{dir.path}/#{filename_scape_characters(change.uid)}"

	  if change.changetype != Opensync::OSYNC_CHANGE_TYPE_DELETED
		hash = generate_hash(File.new(filename));
		change.hash=hash
	  end

	  hashtable.update_change(change)
	  context.report_sucess
      end

      def sync_done(sink, info, ctx, userdata)
	dir=userdata
	state_db = sink.state_db
	state_db.set("path", dir.path)
	ctx.report_success
      end
end

class FileFormat < Opensync::ObjectFormat
    class FileFormatData
	attr_accessor :mode, :userid, :groupid, :last_mod, :path, :data, :size

	def to_s
	    "File #{self.path}: size: #{self.size}"
	end

	def marshal_dump
	  [@mode, @userid, @groupid, @last_mod, @path, @data, @size]
	end

	def marshal_load(array)
	  (@mode, @userid, @groupid, @last_mod, @path, @data, @size) = array
	end

	def to_buf
	    Marshal.dump(self)
	end

	def self.from_buf(string)
	    Marshal.load(string)
	end
    end

    def self.get_format_info(env)
	format = self.new("file", "data")
	env.register_objformat(format)
    end

    def initialize_new(name, objtype)
	# Map the callbacks
        self.compare_func=callback{|format, *args| self._compare(*args) }
	self.destroy_func=callback{|format, *args| self._destroy(*args) }
	self.duplicate_func=callback{|format, *args| self._duplicate(*args) }
	self.print_func=callback{|format, *args| self._print(*args) }
	self.revision_func=callback{|format, *args| self._revision(*args) }
	self.copy_func=callback{|format, *args| self._copy(*args) }
    end

    private

    def _compare(leftdata, rightdata)
      	leftfile = FileFormatData.from_buf(leftdata);
	rightfile = FileFormatData.from_buf(rightdata);

	return Opensync::OSYNC_CONV_DATA_MISMATCH if not leftfile.path == rightfile.path
	return Opensync::OSYNC_CONV_DATA_SIMILAR  if not leftfile.size == rightfile.size
	return Opensync::OSYNC_CONV_DATA_SIMILAR  if leftfile.size>0 and not leftfile.data == rightfile.data
	return Opensync::OSYNC_CONV_DATA_SAME
    end

    # Do I need this?
    def _destroy(data)
    end

    def _duplicate(uid, input)
	file=FileFormatData.from_buf(input)
	file.path="#{copy.path}-dupe"
	[file.path, file.to_buf, true]
    end

    def _copy(input)
	FileFormatData.from_buf(input).dup.to_buf
    end

    def _revision(input)
	FileFormatData.from_buf(input).mtime
    end

    def _print(data)
	FileFormatData.from_buf(data).to_s
    end
end

class PlainFormat < Opensync::ObjectFormat
    def initialize_new(name, objtype)
	self.compare_func=callback{|format, *args| self._compare(*args) }
	self.copy_func=callback{|format, *args| self._copy(*args) }
	self.destroy_func=callback{|format, *args| self._destroy(*args) }
    end

    def _compare(leftdata, rightdata)
	return Opensync::OSYNC_CONV_DATA_MISMATCH if not leftdata == rightdata
	return Opensync::OSYNC_CONV_DATA_SAME
    end

    def _copy(input)
	input.dup
    end

    def _destroy(input)
	#nothing to do?
    end

    def self.get_format_info(env)
	format = self.new("plain", "data")
	env.register_objformat(format)
	# "memo" is the same as "plain" expect the object type is fixed to "note"
	format = self.new("memo", "note")
	env.register_objformat(format)
    end
end

class FilePlainConverter < Opensync::FormatConverter

    def convert_file_to_plain(input)
	FileFormatData.from_buf(input).data.dup
    end

    def convert_plain_to_file(input)
	file=FileFormatData.new
	# TODO
	file.path = Opensync::osync_rand_str(rand(100)+1)
	file.data = input;
	file.size = input.size;
	file.to_buf
    end

    def self.get_conversion_info(env)
	file = env.find_objformat("file") or
	    raise "Unable to find file format"
	plain = env.find_objformat("plain") or
	    raise "Unable to find plain format"

	# TODO: change new method in order to implicitily call the callback (or something better)
	conv = FilePlainConverter.new(Opensync::OSYNC_CONVERTER_DECAP, file, plain, Proc.new {|| puts "convert!"}) or
	    return false
	env.register_converter(conv)

	conv = FilePlainConverter.new(Opensync::OSYNC_CONVERTER_ENCAP, plain, file, Proc.new {|| puts "convert!"}) or
	    return false
	env.register_converter(conv)

	return true
    end
end

# BUG: If the MetaPlugin method is incorrect, it segfaults
Opensync::MetaPlugin.register(RubyFileSync)
Opensync::MetaFormat.register(FileFormat)
Opensync::MetaFormat.register(PlainFormat)
Opensync::MetaFormat.register(FilePlainConverter)