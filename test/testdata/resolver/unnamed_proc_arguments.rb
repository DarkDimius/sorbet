# typed: true

# this one is OK
T.let(nil, T.nilable(T.proc.params(x: Integer).void))

# this one fails because the proc doesn't have named params
T.let(nil, T.nilable(T.proc.params(Integer).void))
                   # ^^^^^^^^^^^^^^^^^^^^^^ error: `params` expects keyword arguments
                   # ^^^^^^^^^^^^^^^^^^^^^^ error: `T.class_of(Integer)` does not match `T::Hash[T.untyped, T.untyped]`
