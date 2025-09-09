#!/bin/bash

set -ex

bundle exec ruby -I ext/pycall pycall_hangs_main_thread.rb 2> /dev/null