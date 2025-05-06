# coding: utf-8

require_relative 'lib/pycall/version.rb'

Gem::Specification.new do |spec|
  spec.name          = "pycall"
  spec.version       = PyCall::VERSION
  spec.authors       = ["Kenta Murata"]
  spec.email         = ["mrkn@mrkn.jp"]

  spec.summary       = %q{pycall}
  spec.description   = %q{pycall}
  spec.homepage      = "https://github.com/red-data-tools/pycall.rb"
  spec.license       = "MIT"

  spec.files         = `git ls-files -z`.split("\x0").reject do |f|
    case f
    when %r{^ext/pycall/spec_helper/},
         %r{^(test|spec|features)/}
      true
    else
      false
    end
  end

  spec.bindir        = "exe"
  spec.executables   = spec.files.grep(%r{^exe/}) { |f| File.basename(f) }
  spec.require_paths = ["lib"]
  spec.extensions    = ["ext/pycall/extconf.rb"]

  spec.add_dependency 'fiddle', '>= 1.0.0'

  spec.add_development_dependency "bundler"
  spec.add_development_dependency "rake"
  spec.add_development_dependency "rake-compiler"
  spec.add_development_dependency "rake-compiler-dock"
  spec.add_development_dependency "rspec"
  spec.add_development_dependency "launchy"
  spec.add_development_dependency "pry"
  spec.add_development_dependency "pry-byebug"
  spec.add_development_dependency "test-unit"
end
