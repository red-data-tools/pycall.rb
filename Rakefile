require "bundler/gem_helper"
require "rake/clean"

base_dir = File.join(File.dirname(__FILE__))

helper = Bundler::GemHelper.new(base_dir)
helper.install
spec = helper.gemspec

def run_extconf(build_dir, extension_dir, *arguments)
  cd(build_dir) do
    ruby(File.join(extension_dir, "extconf.rb"), *arguments)
  end
end

def make_command
  if RUBY_PLATFORM =~ /mswin/
    "nmake"
  else
    ENV["MAKE"] || find_make
  end
end

def find_make
  candidates = ["gmake", "make"]
  paths = ENV.fetch("PATH", "").split(File::PATH_SEPARATOR)
  exeext = RbConfig::CONFIG["EXEEXT"]
  candidates.each do |candidate|
    paths.each do |path|
      cmd = File.join(path, "#{candidate}#{exeext}")
      return cmd if File.executable?(cmd)
    end
  end
end

Dir[File.expand_path('../tasks/**/*.rake', __FILE__)].each {|f| load f }

spec.extensions.each do |extension|
  extension_dir = File.join(base_dir, File.dirname(extension))
  build_dir = ENV["BUILD_DIR"]
  if build_dir
    build_dir = File.join(build_dir, "pycall")
    directory build_dir
  else
    build_dir = extension_dir
  end

  makefile = File.join(build_dir, "Makefile")
  file makefile => build_dir do
    run_extconf(build_dir, extension_dir)
  end

  CLOBBER << makefile
  CLOBBER << File.join(build_dir, "mkmf.log")

  desc "Configure"
  task configure: makefile

  desc "Compile"
  task compile: makefile do
    cd(build_dir) do
      sh(make_command)
    end
  end

  task :clean do
    cd(build_dir) do
      sh(make_command, "clean") if File.exist?("Makefile")
    end
  end
end

require "rake/extensiontask"
Rake::ExtensionTask.new("pycall/spec_helper")

desc "Run tests"
task :test do
  cd(base_dir) do
    ruby("test/run-test.rb")
  end
end

task default: :test

require "rspec/core/rake_task"
RSpec::Core::RakeTask.new(:spec) do |t|
  ext_dir = File.join(base_dir, "ext/pycall")
  t.ruby_opts = "-I#{ext_dir}"
  t.verbose = true
end

task default: :spec
task spec: :compile


release_task = Rake.application["release"]
# We use Trusted Publishing.
release_task.prerequisites.delete("build")
release_task.prerequisites.delete("release:rubygem_push")
release_task_comment = release_task.comment
if release_task_comment
  release_task.clear_comments
  release_task.comment = release_task_comment.gsub(/ and build.*$/, "")
end
