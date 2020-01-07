require 'tempfile'
require 'sexp_processor'
require_relative './compiler/pass1'
require_relative './compiler/pass2'
require_relative './compiler/pass3'

module Natalie
  class Compiler
    SRC_PATH = File.expand_path('../../src', __dir__)
    OBJ_PATH = File.expand_path('../../obj', __dir__)
    MAIN_TEMPLATE = File.read(File.join(SRC_PATH, 'main.c'))
    OBJ_TEMPLATE = <<-EOF
      #{MAIN_TEMPLATE.scan(/^#include.*/).join("\n")}

      /*TOP*/

      NatObject *obj_%{name}(NatEnv *env, NatObject *self) {
        /*BODY*/
        return env_get(env, "nil");
      }
    EOF

    class RewriteError < StandardError; end

    def initialize(ast, path)
      @ast = ast
      @var_num = 0
      @path = path
    end

    attr_accessor :ast, :compile_to_object_file, :shared, :out_path

    attr_writer :load_path

    def compile
      check_build
      write_file
      cmd = compiler_command
      out = `#{cmd}`
      File.unlink(@c_path) unless ENV['DEBUG']
      $stderr.puts out if ENV['DEBUG'] || $? != 0
      raise 'There was an error compiling.' if $? != 0
    end

    def c_files_to_compile
      Dir[File.join(SRC_PATH, '*.c')].grep_v(/main\.c$/)
    end

    def check_build
      if Dir[File.join(OBJ_PATH, '*.o')].none?
        puts 'please run: make build'
        exit 1
      end
    end

    def write_file
      c = to_c
      puts c if ENV['DEBUG']
      temp_c = Tempfile.create('natalie.c')
      temp_c.write(c)
      temp_c.close
      @c_path = temp_c.path
    end

    def transform(ast)
      p1 = Pass1.new
      p1.var_prefix = var_prefix
      r1 = p1.rewrite(ast)
      p2 = Pass2.new
      p2.var_prefix = var_prefix
      p2.var_num = p1.var_num
      result = p2.process(r1)
      [
        Pass3.new(p2.top).process,
        Pass3.new(p2.decl + "\n" + result).process,
      ]
    end

    def to_c
      @ast = expand_macros(@ast, @path)
      (top, body) = transform(@ast)
      out = template
        .sub('/*TOP*/', top)
        .sub('/*BODY*/', body)
      reindent(out)
    end

    def load_path
      Array(@load_path)
    end

    private

    def compiler_command
      if compile_to_object_file
        "gcc -I #{SRC_PATH} -x c -c #{@c_path} -o #{out_path} 2>&1"
      else
        "gcc -g -Wall #{shared ? '-fPIC -shared' : ''} -lgc -I #{SRC_PATH} -o #{out_path} #{OBJ_PATH}/*.o #{OBJ_PATH}/language/*.o -x c #{@c_path} 2>&1"
      end
    end

    def var_prefix
      if compile_to_object_file
        "#{obj_name}_"
      else
        ''
      end
    end

    def obj_name
      out_path.sub(/.*obj\//, '').sub(/\.o$/, '').tr('/', '_')
    end

    def template
      if compile_to_object_file
        OBJ_TEMPLATE % { name: obj_name }
      else
        MAIN_TEMPLATE
      end
    end

    def expand_macros(ast, path)
      (0...(ast.size)).reverse_each do |i|
        node = ast[i]
        if macro?(node)
          ast[i,1] = run_macro(node, path)
        end
      end
      ast
    end

    def macro?(node)
      return false unless node[0..1] == s(:call, nil)
      %i[require require_relative load].include?(node[2])
    end

    def run_macro(expr, path)
      (_, _, macro, *args) = expr
      send("macro_#{macro}", *args, path)
    end

    def macro_require(node, current_path)
      path = node[1] + '.nat'
      macro_load([nil, path], current_path)
    end

    def macro_require_relative(node, current_path)
      path = File.expand_path(node[1] + '.nat', File.dirname(current_path))
      macro_load([nil, path], current_path)
    end

    def macro_load(node, _)
      path = node.last
      full_path = if path.start_with?('/')
                    path
                  else
                    load_path.map { |d| File.join(d, path) }.detect { |p| File.exist?(p) }
                  end
      if full_path
        code = File.read(full_path)
        file_ast = Natalie::Parser.new(code, full_path).ast
        expand_macros(file_ast, full_path)
      else
        raise LoadError, "cannot load such file -- #{path}"
      end
    end

    def reindent(code)
      out = []
      indent = 0
      code.split("\n").each do |line|
        indent -= 4 if line =~ /^\s*\}/
        out << line.sub(/^\s*/, ' ' * indent)
        indent += 4 if line.end_with?('{')
      end
      out.join("\n")
    end
  end
end
