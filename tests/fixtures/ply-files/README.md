# Typed PLY file fixtures

These small original Apache-2.0 fixtures cover all eight scalar wire types,
ordered dense and list properties, signed zero, a minimum subnormal, an unused
vertex and indexed polygon rows. The ASCII fixture preserves mixed line endings
and a noncanonical comment. The two `.ply.hex` files encode the exact binary PLY
source bytes as lowercase hexadecimal text; file tests decode them before writing
physical capture inputs. Hexadecimal whitespace accepts LF and CRLF checkouts.

Changed chains explicitly select Geometry face rows 0 and 2. The producer tests
verify the complete typed values, wire schema, encoding, row order, receipt and
physical directory inventory after publication.
