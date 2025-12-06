#pragma once

/*

Safe Integers.
Licence: BSD 4-clause.

Read comments/docs on the main class - ecul::safe_int. See end of file for background and correctness.

Various information here and quotes might be copied (as fair-use)) from C++ standards ISO (and the draft) which may include documents such as:
C++11 (ISO/IEC 14882:2011, draft in N3337)
C++14 (ISO/IEC 14882:2014, draft in N4140)
C++17 (ISO/IEC 14882:2017, draft in N4659)
C++20 (ISO/IEC 14882:2020, draft in N4860)
C++23 (ISO/IEC 14882:2023, draft in N4959)
- All C++ standards released by the ISO/IEC JTC1/SC22/WG21 C++ Standards Committee.

*/

#include <type_traits>
#include <limits>


#if defined(__GNUC__) || defined(__clang__)
    #define ATTRIB_PURE __attribute__((pure))
#else
    #define ATTRIB_PURE
#endif

namespace ecul {

template <typename A, typename B>
bool equal_to(A a, B b) {
    if constexpr (std::is_signed_v<A> && std::is_unsigned_v<B>) {
        if (a < 0) return false; // negative signed cannot equal any unsigned
        return static_cast<std::make_unsigned_t<A>>(a) == b;
    } else if constexpr (std::is_unsigned_v<A> && std::is_signed_v<B>) {
        if (b < 0) return false; // negative signed cannot equal any unsigned
        return a == static_cast<std::make_unsigned_t<B>>(b);
    } else {
        // both signed or both unsigned
        return a == b;
    }
}

/*
Absolutely safe (see below) integral type, using just pure C/C++ (no libs) on all existing platforms.
For C++14 (and § rules refer to that standard) should work with new ones too (C++23 at time of writing).
It wraps an integral (built-in) type, which is also called the "value" of this object, "decays" to it (conversion operator), and provides operators that
must handle errors.
All operations where we are left-side (or only) operand, and the right-side (if any) is either another object of our class, or an simple-integer,
meet one of following:
1) consider our value (A), and value B of the other operand (if any). If the result of simply doing that operation on A,B (or just A if single-operand)
would be imperfect (see below), then handle error (or compile time error with diagnostics).
2) otherwise, the operation must return the correct result (integral value or true/false) that is same the rules of mathemathics.

All of the checks internally must not cause any imperfect operations in any cases, for any combination of types of the operands and values of operands.

@par supported actions:
- construct from an integral
- convert to the integer of wrapped-type
- entire family of addition/subtraction (along with negation) + += ++ - -= -- along with negation (-x), and the increment/decrement in both orders
- entire family of equality comparisons (== !=) and sorting comparisons (< > <= >=)

@par handling-errors

@par terms
- imperfect calculation means that either:
  - it would be an UB to have that expression/calculation
  - or the result of operation is different from mathemathical definition (usually due to overflow/underflow)
  - or if wrapping occurs (EVEN the legal wrapping of unsigned integrals §5 [expr]/4. Even though it is legal, still is not allowed)

@par safe

@par notation
- remark (^) in entire this class - when we write examples on numerical values e.g. (-128..+127), (0..255) etc, this is on example of (un)signed char.

*/

template <typename TA>
class safe_int {
    static_assert( std::is_integral_v<TA> , "We can wrap only integral types");

    private:
        template <typename T> static constexpr bool is_signed = std::is_signed_v<T>;
        template <typename T> static constexpr bool is_unsigned = !std::is_signed_v<T>;

        /// call this two only AFTER making sure the value will fit into the converted type.
        template <typename T> constexpr static std::make_unsigned_t<T> unsafe_cast_to_unsigned(T x) ATTRIB_PURE { return static_cast<std::make_unsigned_t<T>>(x); }
        template <typename T> constexpr static std::make_signed_t<T> unsafe_cast_to_signed(T x) ATTRIB_PURE { return static_cast<std::make_signed_t<T>>(x); }

    public:

        template<typename TB> bool operator==(TB b) noexcept {
            auto & a = this->val;
            if constexpr (is_signed<TA> && is_unsigned<TB>) {
                if (a<0) return false; // A is negative while B can't be, so return
                return unsafe_cast_to_unsigned(a) == b; // (^) A is (0..127) ---> casted into var 0..255, so always safe
            }
            else if constexpr (is_unsigned<TA> && is_signed<TB>) {
								if (b<0) return false; // B is negative while A can't be, so return
								return unsafe_cast_to_unsigned(b) == a; // (^) B is (0..127) ---> casted into var 0..255, so always safe
            }
            else return a == b; // same signedness, ok - [compare same-signedness]
        }

    private:
        TA val;

}; // class

} // namespace

/*

*** [compare same-signedness]

C++14 guarantees that comparing two built‑in integral types of the same signedness is always well‑defined and logically correct. When both operands are signed,
or both are unsigned, the rules of integral promotion and the usual arithmetic conversions (§5.9 Relational operators) ensure that the narrower type is promoted
to the wider type before the comparison. This means the comparison behaves exactly as expected, without surprises.

The draft explicitly cautions that mixing signed and unsigned operands can lead to unexpected results. As §5.9 notes, if the operands differ in signedness, the
signed operand may be converted to unsigned, which can produce counter‑intuitive outcomes (e.g. -1 < 1u evaluates to false).  Supporting Quotes (C++14 Draft
N4140)

		§5.9 Relational operators: “If the operands have different types, the usual arithmetic conversions are performed to bring them to a common type.”

		§4.5 Integral promotions: “A prvalue of an integer type … whose integer conversion rank is less than that of int can be converted to a prvalue of type int
		if int can represent all the values of the source type; otherwise, it can be converted to unsigned int.”

*/
