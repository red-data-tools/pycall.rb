"""Helpers for the specs of callable attributes (GitHub issue #87)."""

import functools


class FrozenDistribution(object):
    def __init__(self, loc, scale):
        self.loc = loc
        self.scale = scale

    def pdf(self, x):
        return 'frozen pdf(%r) loc=%r scale=%r' % (x, self.loc, self.scale)


class DistributionGenerator(object):
    """Mimics scipy.stats.rv_continuous: a callable instance whose __call__
    returns a frozen distribution with fixed parameters."""

    def __call__(self, loc=0, scale=1):
        return FrozenDistribution(loc, scale)

    def pdf(self, x, loc=0, scale=1):
        return 'generator pdf(%r) loc=%r scale=%r' % (x, loc, scale)


# Like scipy.stats.norm
norm = DistributionGenerator()


def zero_arg_function():
    return 'function called'


@functools.lru_cache(maxsize=None)
def cached_zero_arg_function():
    return 'cached function called'


partial_function = functools.partial(lambda x: 'partial called with %s' % x, 42)

builtin_method = 'abc'.upper

method_wrapper = (42).__repr__


class DescriptorCallable(object):
    """A callable non-data descriptor, which inspect.isroutine() accepts."""

    def __get__(self, obj, objtype=None):
        return self

    def __call__(self):
        return 'descriptor called'


descriptor_callable = DescriptorCallable()


class Target(object):
    norm = norm

    def __init__(self):
        self.instance_norm = DistributionGenerator()

    def zero_arg_method(self):
        return 'method called'

    @staticmethod
    def zero_arg_staticmethod():
        return 'staticmethod called'

    @classmethod
    def zero_arg_classmethod(cls):
        return 'classmethod called on %s' % cls.__name__
