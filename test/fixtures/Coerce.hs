{-# LANGUAGE GADTs #-}
{-# LANGUAGE TypeFamilies #-}

-- Source for generated Core fixtures (see `just gen-corpus`). Exercises the
-- coercion surface the simpler fixtures lack: newtype representation coercions
-- and `coerce` (casts), type-family axioms, and GADT equality evidence.
module Coerce where

import Data.Coerce (coerce)

newtype Age = Age Int

inc :: Age -> Age
inc (Age n) = Age (n + 1)

ages :: [Int] -> [Age]
ages = coerce

type family F a where
  F Int = Bool
  F Bool = Int

data G a where
  GI :: Int -> G Int
  GB :: Bool -> G Bool

evalG :: G a -> a
evalG (GI n) = n
evalG (GB b) = b
