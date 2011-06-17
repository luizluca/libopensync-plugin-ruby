#!/usr/bin/ruby

#
# WARN: plugin_data is set on sync_get_info
# DONE: Add C counterpart for Sink methods
# DONE: Convert the use of error into an simple Exception
# DONE: Convert OSyncList to Array of what?
# TODO: Cannot use GC to clean objects. Must to it manually. Inc the ref counter in order to avoid GC
#       - clean sink ruby on plugin destroy
#         

# http://opensync.org/wiki/glossary
# http://metaeditor.sourceforge.net/embed/#id2842525
# http://www.ruby-doc.org/docs/ProgrammingRuby/html/ext_ruby.html
# http://people.apache.org/~rooneg/talks/ruby-extensions/ruby-extensions.html

# $stderr.puts "Starting"

#require "opensync.so"

require "thread"
require "set"

$trace=true

if $trace    
  @untraced_classes=Set.new
  @untraced_classes.merge(Object.constants.collect {|sym| Object.const_get(sym) }.select{|cons| cons.kind_of? Module }) 
end

module Opensync      

    # TODO: Remove this
    class OSyncError < Exception
    end

    # This is not really a plugin but someone that ruby-module calls to run get_sync_info
    class MetaPlugin
        def self.get_sync_info(_env)	  
	    # Non OO example
	    Example1.get_sync_info(_env)
	    
	    # OO example
	    env=Plugin::Env.from(_env)
	    RubyFileSync.get_sync_info(env)
	    true
        end	  
    end
    
    
    # This is not really a format but someone that ruby-module calls to run get_format_info and get_conversion_info
    class MetaFormat      
        def self.get_format_info(_env)
	    env=Format::Env.from(_env)
	    FileFormat.get_format_info(env)
	    PlainFormat.get_format_info(env)
	    true
        end
	
	def self.get_conversion_info(_env)
	    env=Format::Env.from(_env)
	    FilePlainConverter.get_conversion_info(env)
	    true
	end
    end
    
    # This is a simple example that uses no object oriented. It just call osync like in C
    class Example1
        # This is called in order to alloc and create a new plugin
        def self.get_sync_info(_env)
	    # TODO: unref plugin and call ruby_free on GC
	    result=false	    
	    _plugin = Opensync.osync_plugin_new	    
	    begin	
		#Opensync.osync_plugin_ruby_init(_plugin)
		Opensync.osync_plugin_set_name(_plugin, "example1")
		Opensync.osync_plugin_set_longname(_plugin, "Example module using direct osync methods")
		Opensync.osync_plugin_set_description(_plugin, "This module uses no object oriented logic")
		Opensync.osync_plugin_set_initialize_func(_plugin, Proc.new{|_plugin,_info| initialize(_plugin,_info)});
		Opensync.osync_plugin_set_finalize_func(_plugin, Proc.new{|_plugin,_info| finalize(_plugin,_info) });
		Opensync.osync_plugin_set_discover_func(_plugin, Proc.new{|_plugin,_info| discover(_plugin,_info) });	    
		if not Opensync.osync_plugin_env_register_plugin(_env, _plugin)
		   raise OSyncExceptionInitialization("Failed to register #{self}")
		end
		#Opensync.osync_plugin_env_load(_env, "xxx");
		result=true
	    ensure
		Opensync.osync_plugin_unref(_plugin)
	    end
	    return result
	end
	
	def self.initialize(_plugin,_info)
	    $stderr.puts "Initializing #{self}"
	end

	def self.finalize(_plugin,_info)
	    $stderr.puts "Finalizing   #{self}"
	end
	
	def self.discover(_plugin,_info)
	    $stderr.puts "Discovering  #{self}"
	end
    end        
    
    
    # Ruby way of doing it
    
    class OSyncObject
      	# The self.for creates a ruby object to represent a C data	
	class << self
	    alias_method :new_pvt, :new
	    
	    def class_method_defined?(symbol)
 		self.methods.include?(symbol)
	    end
	end		
	
	def self.alloc
	    raise "#{self.class} must implement a alloc method in order to alloc a new object"
	end

	def self.unref(obj)
	    raise "#{self.class} must implement a unref method in order to avoid memory leaks"
	end
	
	def self.ref(obj)
	    raise "#{self.class} might be implement"
	end
      
# 	def self.need_ruby_init?
# 	    class_method_defined?(:ruby_init)
# 	end	
	
	def self.cleanup_on_GC(obj,_self)
	    destructor=Proc.new do
		# cleanup C world userdata
		#self.ruby_free(_self) if need_ruby_init?
		self.unref(_self)
	    end
	    ObjectSpace.undefine_finalizer(obj)	    	    
	    ObjectSpace.define_finalizer(obj, destructor)    
	end

	NEW=:new
	def self.new(*args)
	    new_pvt(NEW,*args)
	end
	
	def self.from(_self)
	    # Maybe rescue a previous instance
	    new_pvt(_self)
	end
			
	attr_reader :_self
	#
	# TODO: update this doc
	# Initialize an object in Ruby World. _self represents the SWIK object.
	# If _self is nil, a new object is allocated. Also, it sets a trigger
	# to call unref on _self when it is Garbage collected
	#
	def initialize(_self, *args)
	    case _self
	    when NEW
		@_self = self.class.alloc(*args)		
		self.class.cleanup_on_GC(self,@_self)		
	    else
		@_self=_self
		self.class.ref(@_self)
		self.class.cleanup_on_GC(self,@_self)		
	    end
	    
	    klass=@_self.class
	    if not @@swig2ruby[klass]
		raise "#{self} must be associated with #{klass} before using class.represent"
	    end
	    if not self.class==@@swig2ruby[klass] and not self.class.ancestors.include?(@@swig2ruby[klass])
		raise "#{klass} is associated with #{@@swig2ruby[klass]} and not #{self.class}"
	    end	    	    
	    
	    #self.class.ruby_init(@_self) if self.class.need_ruby_init? and not ruby_initialized?
	    
	    case _self
	    when NEW
		initialize_new(*args)
	    else
		initialize_from
	    end
	end
	
	def initialize_from
	end

	def initialize_new(*args)	    
	end
	private :initialize_from, :initialize_new	
	
	@@unmapped_methods=Set.new(Opensync.methods.select {|method| /^osync_(?!get_version)/ =~ method.to_s })
# 	@@unmapped_methods.delete("osync_rubymodule_get_data")
# 	@@unmapped_methods.delete("osync_rubymodule_set_data")
# 	@@unmapped_methods.delete("osync_rubymodule_clean_data")
	
	def self.map_methods(regexp)
	    @@unmapped_methods.
	      select {|method| regexp =~ method.to_s }.
	      each do		
		|method|
		@@unmapped_methods.delete(method)
		#$stderr.puts "Defining #{method} for #{self}"
		prefix = method.to_s.match(regexp)[0]
		suffix = method.to_s[prefix.size..-1]
				
# 		$stderr.puts suffix if self.name=="Opensync::ObjectType::Sink"
		
		case suffix		
		  
		# new is already used. Name it alloc
		when "new"
		    self.class_eval "
		    def self.alloc(*args)
			args=args.collect {|arg| if arg.kind_of? OSyncObject; arg._self; else; arg; end}
			Opensync.#{method}(*args)
		    end
		    "
		    
		# class methods
		when "unref", "ref" #, "ruby_init", "ruby_free"
		    self.class_eval "
		    def self.#{suffix}(*args)
			Opensync.#{method}(*args)
		    end
		    "    
# 		when "is_ruby_initialized"
# 		    self.class_eval "
# 		    def self.ruby_initialized?(*args)
# 			Opensync.#{method}(*args)
# 		    end
# 		    "    
		when "initialize"
		    self.class_eval "
		    def osync_initialize(*args)			
			Opensync.#{method}(@_self, *args)
		    end
		    "
		    
		# getters and setters    
		when /^get_/
		    property=suffix[4..-1]
		    # Append 0 to initialize in order to avoid conflict with ruby initialize
		    property="#{property}0" if property == "initialize"
		    self.class_eval "
		    def #{property}
			self.class.map_object(Opensync.#{method}(@_self))
		    end
		    "
		    
		when /^set_/				    
		    property=suffix[4..-1]		    
# 		    $stderr.puts property if self.name=="Opensync::ObjectType::Sink"
		    self.class_eval "
		    def #{property}=(value)
			value=value._self if value.kind_of? OSyncObject			
			Opensync.#{method}(@_self, value)
		    end
		    "
		when /^is_/				    
		    property=suffix[3..-1]		    # 		    
		    self.class_eval "
		    def #{property}?
			value=value._self if value.kind_of? OSyncObject			
			self.class.map_object(Opensync.#{method}(@_self))
		    end
		    "
		else 		    
		    # Append 0 to initialize in order to avoid conflict with ruby initialize
		    property="#{suffix}0" if suffix == "initialize"	
		    self.class_eval "
		    def #{suffix}(*args)
			args=args.collect {|arg| if arg.kind_of? OSyncObject; arg._self; else; arg; end}
			self.class.map_object(Opensync.#{method}(@_self, *args))
		    end
		    "
		end
	      end	      
	end
	
	def self.unmapped_methods
	    @@unmapped_methods.dup
	end
	
	def self.map_object(obj)
	    if obj.kind_of? Array
		obj.collect {|_obj| self.map_object(_obj) }
	    else
		if klass=@@swig2ruby[obj.class]
		    klass.from(obj)
		else
		    obj
		end	   
	    end
	end
	
	@@swig2ruby = {}
	def self.represent(klass)
	    @@swig2ruby[klass]=self
	end
			
	def callback(&block)
	    Proc.new do
	      |*args| 
	      args=self.class.map_object(args)
	      result=block.call(*args)
	      if result.kind_of? OSyncObject
		 result._self
	      else
		 result
	      end
	    end
	end
			
	#
	# Cleans the ruby stuff saved in C world.
	# Generally this should be called before the object retires
	def clean
	    Opensync.osync_rubymodule_clean_data(@_self)
	end
	
	def [](key)
	    Opensync.osync_rubymodule_get_data(@_self, key.to_s)
	end
	
	def []=(key, value)
	    Opensync.osync_rubymodule_set_data(@_self, key.to_s, value)
	end	  
    end
        
    class Plugin < OSyncObject	
	map_methods /^osync_plugin_((?!env)(?!info)(?!config)(?!advancedoption)(?!authentication)(?!connection)(?!localization)(?!resource))/		
	represent SWIG::TYPE_p_OSyncPlugin
	
	attr_reader :info
	
	def self.get_sync_info(env)
	    plugin = self.new       
	    env.register_plugin(plugin)
	end

	class Env < OSyncObject
	    represent SWIG::TYPE_p_OSyncPluginEnv
	    map_methods /^osync_plugin_env_/
	end
	
	class Info < OSyncObject	   
	    map_methods /^osync_plugin_info_/
	    represent SWIG::TYPE_p_OSyncPluginInfo	    
	end
	
	class Config < OSyncObject
	    represent SWIG::TYPE_p_OSyncPluginConfig
	    map_methods /^osync_plugin_config_/
	end
	
	class Resource < OSyncObject	    
	    map_methods /^osync_plugin_resource_/
	    represent SWIG::TYPE_p_OSyncPluginResource
	end
	
	class Connection< OSyncObject	    
	    map_methods /^osync_plugin_connection_/
	    represent SWIG::TYPE_p_OSyncPluginConnection
	end	
	
	class Authentication < OSyncObject	    
	    map_methods /^osync_plugin_authentication_/
	    represent SWIG::TYPE_p_OSyncPluginAuthentication
	end	
	
	class Localization < OSyncObject	    
	    map_methods /^osync_plugin_localization_/
	    represent SWIG::TYPE_p_OSyncPluginLocalization
	end	
	
	class AdvancedOption < OSyncObject	    
	    map_methods /^osync_plugin_advancedoption_/
	    represent SWIG::TYPE_p_OSyncPluginAdvancedOption
	end	
    end
            
    class ObjectFormat < OSyncObject
	map_methods /^osync_objformat_((?!sink))/
	represent SWIG::TYPE_p_OSyncObjFormat
	
	class Sink < OSyncObject
	    map_methods /^osync_objformat_sink_/
	    represent SWIG::TYPE_p_OSyncObjFormatSink	    
	end
    end
      
    class FormatConverter < OSyncObject
	map_methods /^osync_converter_/
	represent SWIG::TYPE_p_OSyncFormatConverter
    end
        
    class Data < OSyncObject
	map_methods /^osync_data_/
	represent SWIG::TYPE_p_OSyncData
    end
    
    class Context < OSyncObject
	map_methods /^osync_context_/
	represent SWIG::TYPE_p_OSyncContext
    end
    
    class ObjectType < OSyncObject
	class Sink < OSyncObject
	    map_methods /^osync_objtype_sink_/
	    represent SWIG::TYPE_p_OSyncObjTypeSink
	      
	    class MainSink < Sink
		map_methods /^osync_objtype_main_sink/		
	    end
	end
    end  
    
    class SinkState < OSyncObject
	map_methods /^osync_sink_state_/
	represent SWIG::TYPE_p_OSyncSinkStateDB
	
	# Not allocated by _new, not controled
	def self.unref(obj)
	end
	
	def self.ref(obj)
	end
    end
    
    class HashTable < OSyncObject
	map_methods /^osync_hashtable_/
	represent SWIG::TYPE_p_OSyncHashTable
	
	# Not allocated by _new, not controled
	def self.unref(obj)
	end
	
	def self.ref(obj)
	end
    end
    
    class Version < OSyncObject
	represent SWIG::TYPE_p_OSyncVersion
	# TODO
	#map_methods /^osync_objformat_sink_/
    end
        
    class Merger < OSyncObject
	map_methods /^osync_merger_/
	represent SWIG::TYPE_p_OSyncMerger
    end
    
    # TODO: Check if osync_format should be osync_objformat
    class Format < OSyncObject
      	class Env < OSyncObject
	    map_methods /^osync_format_env_/
	    represent SWIG::TYPE_p_OSyncFormatEnv
	end
    end
    
    class CapsConverter < OSyncObject
	map_methods /^osync_caps_converter_/
	represent SWIG::TYPE_p_OSyncCapsConverter
    end        
end

#$plugin=Opensync::GF690
require "~/prog/opensync/libopensync-plugin-ruby-trunk/example/ruby-file-sync.rb"
#$stderr.puts OpenSync

# TODO: osync_objtype_main_sink_new is mapped where? What is it for? Check docs.
#Opensync::OSyncObject.unmapped_methods.to_a.each {|method| warn("Atention! Method #{method} not mapped!") }

if $trace
  set_trace_func proc { |event, file, line, id, binding, klass|   
    next if not Opensync::osync_trace_is_enabled                    
    next if not klass or @untraced_classes.include?(klass) 
        
    case event
    when "raise"
        type=Opensync::TRACE_ERROR
        msg="RUBY #{klass}.#{id} #{$!}\n#{$!.backtrace.join("\n")}"
    when "call", "c-call"
	type=Opensync::TRACE_ENTRY
        args=binding.eval("local_variables.collect {|var| var }.collect{|var| eval(var.to_s)}")
	id_s=id.to_s
	case 
	when (id_s[-1,1] == "=" and args.size==1)
            msg="RUBY #{klass}.#{id_s[0..-2]}=#{args.first.inspect}"
	when (id_s[-2,2] == "[]" and args.size==1)
	    msg="RUBY #{klass}[#{args.first.inspect}]"
	when (id_s[-3,3] == "[]=" and args.size==2)
            msg="RUBY #{klass}[#{args.first.inspect}]=#{args.last.inspect}"
	else              
	    msg="RUBY #{klass}.#{id_s}(#{args.collect{|arg| arg.inspect}.join(",")})"        
	end
    when "return", "c-return"
	type=Opensync::TRACE_EXIT
        msg="RUBY #{klass}.#{id}"
    else
        next
        #$stderr.printf "%8s %s:%-2d %10s %8s\n", event, file, line, id, klass, binding
    end
    Opensync::osync_trace(type, msg)                      
  }
end
