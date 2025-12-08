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
#include "ecul.hpp"

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

template <typename T>
struct result_val final {
	public:
		T m_val;
		bool m_ok=false;

		struct err {}; ///< ctor tag
		struct ok  {}; ///< ctor tag

		explicit result_val(err) : m_ok(false) { }
		explicit result_val(ok, T val) : m_val(val), m_ok(true) { }
		bool is_ok() const noexcept { return m_ok; }
		T unwrap_or_throw() const {
			if (m_ok) return m_val;
			throw ecul_erro_runtime("Unwrapping value from error result.");
		}
};

template <typename T>
result_val<T> nagate_number_result(T val) {
	auto err = []() { return result_val<T>(typename result_val<T>::err{}); };
	auto ok  = [](T v) { return result_val<T>(typename result_val<T>::ok{} , v ); };

	static_assert(std::is_integral_v<T>, "Works only on integers");
	if constexpr (std::is_unsigned_v<T>) {
		if (val==0) return ok(val); // returning same value, no-op, obviously safe
		return err(); // throw ecul_erro_runtime("Can not negate (a non zero value) of an unsigned type");
	} else { // signed
		if (val == std::numeric_limits<T>::min()) {
			return err(); // throw ecul_erro_runtime("Can not negate a value (signed) ==min() as it would overflow allowed max()");
		}
		// as for signed integrals, is allowed in other cases, problem is only with the min()
		// see C++14 (draft) [expr.unary.op] Unary operators — §5.3.1/8 "The negative of the smallest representable value of the type is not representable;
		// thus, negating that value is undefined."
		return ok( - val ); // <--- the normal negation
	}
}



// return (2^n) that is, take 2 to power with exponent n given to function. N must be not-negative, so 0 up to limit
// the limit is what ever can fit in the type <T>, otherwise returns error.
template <typename T, typename TExpo>
result_val<T> pow2_int_result(TExpo expon) {
	auto err = []() { return result_val<T>(typename result_val<T>::err{}); };
	auto ok  = [](T v) { return result_val<T>(typename result_val<T>::ok{} , v ); };

	if (expon < 0) return err(); // illegal
	if constexpr (std::numeric_limits<T>::digits == 1) { // pretty silly case but allowed
		//if ( ! constexpr std::is_same_v<T,bool> ) return resul
		static_assert( std::is_same_v<T,bool> , "pow2_int T with 1 bit size, it must be a boolean then.");
		if (expon==0) return ok( 1 ); // 2^1 == 1
		if (expon>=1) return err(); // reult, 2, can't fit in bool (same for bigger, if they would be possible)
	}
	else {
		// T has 2 or more bits.
		constexpr auto expon_max_allowed = []() constexpr {
			// max exponent allowed, on example on unsigned char:
			// 7 for unsigned char (2^8==256 can't fit 0..255,  but one less fits)
			// 6 for   signed char (2^7==128 can't fit -128..127, but one less fits)
			static_assert( std::numeric_limits<T>::digits >= 2 ); // double-check
			// (on example of 8 bit unsigned/signed char)
			if constexpr (std::is_unsigned_v<T>) return std::numeric_limits<T>::digits -1; // 8 bits -> allow 7 exponent
			if constexpr (std::is_signed_v<T>  ) return std::numeric_limits<T>::digits -2; // 8 bits -> allow 6 exponent
		}();
		if (expon >= expon_max_allowed) return err(); // throw std::runtime_error("Too big exponnet in power-of-2 integer");
		return ok( static_cast<T>(1) << expon ); // <--- the normal power ( 2 ^ expon )
	}
}

// return +=1 for the given integer T, if the result would fit and be correct without any (even legal) wrapping, else return error.
template <typename T>
result_val<T> int_plus1(T val) {
	auto err = []() { return result_val<T>(typename result_val<T>::err{}); };
	auto ok  = [](T v) { return result_val<T>(typename result_val<T>::ok{} , v ); };

	if (val < (std::numeric_limits<T>::max())) return ok(val+=1);
	return err();
}

// return -=1 for the given integer T, if the result would fit and be correct without any (even legal) wrapping, else return error.
template <typename T>
result_val<T> int_minus1(T val) {
	auto err = []() { return result_val<T>(typename result_val<T>::err{}); };
	auto ok  = [](T v) { return result_val<T>(typename result_val<T>::ok{} , v ); };

	if (val < (std::numeric_limits<T>::max())) return ok(val-=1);
	return err();
}

template<class> struct is_safe_int : std::false_type {};
template<class T> struct is_safe_int<safe_int<T>> : std::true_type {};
template<class T> constexpr bool is_safe_int_v  = is_safe_int<T>::value ;

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
	static_assert( std::is_integral_v<TA> , "We can protect only integral types");

	private:
			template <typename T> static constexpr bool is_signed = std::is_signed_v<T>;
			template <typename T> static constexpr bool is_unsigned = !std::is_signed_v<T>;
			template <typename T> using type_unsigned = std::make_unsigned_t<T>;
			template <typename T> using type_signed   = std::make_unsigned_t<T>;
			/// call this two only AFTER making sure the value will fit into the converted type.
			template <typename T> constexpr static type_unsigned<T> unsafe_cast_to_unsigned(T x) ATTRIB_PURE { return static_cast<std::make_unsigned_t<T>>(x); }
			template <typename T> constexpr static type_signed<T> unsafe_cast_to_signed(T x) ATTRIB_PURE { return static_cast<std::make_signed_t<T>>(x); }

	public:

		using value_t = TA;

		template <typename TB> safe_int(TB b) { (*this) = b; }

		template<typename TB> safe_int<TA>& operator=(TB b) {
			// XXX TODO check if safe assigment
			this->m_val = b;
			return *this;
		}

		template<typename TB> bool oper_equal(TB b) noexcept {
			auto & a = this->m_val;
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

		template<typename TB> bool operator==(TB b) noexcept {
			// TODO
		}

		template<typename TB> bool oper_less<(TB b) {
			auto & a = this->m_val;
			if constexpr (is_signed<TA> && is_unsigned<TB>) {
					if (a<0) return true; // A is negative while B can't be, so return (e.g. -5 < 1)
					return unsafe_cast_to_unsigned(a) > b; // (^) A is (0..127) ---> casted into var 0..255, so always safe
			}
			else if constexpr (is_unsigned<TA> && is_signed<TB>) {
					if (b<0) return false ; // B is negative while A can't be, so return (e.g. 1 < -5)
					return unsafe_cast_to_unsigned(b) < a; // (^) B is (0..127) ---> casted into var 0..255, so always safe
			}
			else return a > b; // same signedness, ok - [compare same-signedness]
		}

		template<typename TB> bool operator!=(TB b) {
			auto & a = this->m_val;
			return !(a == b);
		}

		template<typename TB> bool operator<=(TB b) {
			auto & a = this->m_val;
			return (a < b) || (a == b);
		}

		template<typename TB> bool operator>(TB b) {
			auto & a = this->m_val;
			return !(a < b) && !(a == b);
		}

		template<typename TB> bool operator>=(TB b) {
			auto & a = this->m_val;
			return !(a < b);
		}

		template<typename TB> std::common_type_t<TA, TB> operator+(TB b) noexcept {
			auto & a = this->m_val;
			using TC = std::common_type_t<typename TA::value_t, typename TB::value_t>;
			using mkstr = ecul::mkstr;
			auto show_T = []() -> std::string  {
				return mkstr() << (ecul::nice_type::nice_var<TA>()) << " + " << (ecul::nice_type::nice_var<TB>());
			};

			if constexpr (is_unsigned<TA> && is_unsigned<TB>) {
				// going up, just limit where we can start at most to not overflow
 				// This is safe (x-y) where y is unsigned same as x, and x is wider or same:
				auto T = std::numeric_limits<typename TC::value_t>::max() - b;
				if (a > T) throw ecul_erro_runtime(mkstr()<<"Overflow "<<a<<" + "<<b<<", "<<show_T);
				ecul_abort("noot imple TODO");
			} 
			else if constexpr (is_unsigned<TA> && is_signed<TB>) {
				ecul_abort("noot imple TODO");
			}
			ecul_abort("noot imple TODO");
		}

		template<typename TB> safe_int<TA> & operator+=(TB b) noexcept {
			(*this) = (*this) + b;
			return *this;
		}


		TA val() const { return m_val; }

	private:
			TA m_val;

}; // class



//std::common_type_t< std::result_of_t(TA::val()) , std::result_of_t(TB::val()) safeint_max(TA,TB) {

template<typename TA, typename TB>
std::common_type_t<typename TA::value_t , typename TB::value_t> safeint_max(TA a, TB b) {
	if (a>b) return a;
	return b;
}


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
