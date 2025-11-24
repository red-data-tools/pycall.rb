#!/usr/bin/env ruby

side_thread = Thread.new do
  require 'pycall'
  PyCall.import_module('sys')
  PyCall.import_module('pandas')

  PyCall.finalize # if this line is commented out, the process will hang on exit
  puts "side thread: exiting"
end
side_thread.join

puts "main thread: exiting"
#=> process exits!