require "spec_helper"
require "rbconfig"
require "timeout"

RSpec.describe "GC of PyCall::PyPtr on non-main threads" do
  before do
    # On CPython >= 3.12 the current thread state became a real thread-local,
    # so *any* PyCall call from a thread other than the one that initialized
    # Python crashes with a segmentation fault before the GC scenario below
    # can even be reached.  That pre-existing crash is unrelated to the GC
    # deadlock this spec guards against, so skip there.
    if Gem::Version.new(PyCall::PYTHON_VERSION) >= Gem::Version.new("3.12")
      skip "PyCall cannot be called from non-main threads on Python >= 3.12"
    end
  end

  it "does not deadlock when the GC sweeper frees a PyPtr on a thread that does not hold the GIL" do
    # The thread that initializes Python keeps the GIL for the lifetime of
    # the process, so waiting for the GIL inside the GC sweeper on any other
    # thread freezes the whole VM.  Run the scenario in a child process and
    # fail (instead of hanging the suite) if it does not finish in time.
    script = <<~RUBY
      require "pycall"
      PyCall.import_module("sys")
      Thread.new {
        500.times { PyCall.eval("object()") }
        GC.start
      }.join
      GC.start
      puts "ok"
    RUBY

    base_dir = File.expand_path("../..", __dir__)
    command = [
      RbConfig.ruby,
      "-I", File.join(base_dir, "lib"),
      "-I", File.join(base_dir, "ext/pycall"),
      "-e", script
    ]

    output = nil
    IO.popen(command, err: %i[child out]) do |io|
      begin
        Timeout.timeout(30) { output = io.read }
      rescue Timeout::Error
        Process.kill(:KILL, io.pid)
        raise "deadlocked while freeing a PyPtr on a non-main thread"
      end
    end

    expect(output).to include("ok")
    expect($?).to be_success
  end
end
