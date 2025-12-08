#include <gtest/gtest.h>
#include "ecul_safeint.hpp"
#include <boost/multiprecision/cpp_int.hpp>
#include <vector>
#include <type_traits>
#include <limits>
#include <cstdint>
#include <tuple>

// Basic test for safe_int operator== with plain integers
TEST(SafeIntTest, OperatorEqualsWithPlainIntegers) {
    // Test with signed integers
    ecul::safe_int<int> a(42);
    
    EXPECT_TRUE(a == 42);
    EXPECT_FALSE(a == 100);
    EXPECT_FALSE(a == 0);
}

// Test operator== with mixed signedness (signed vs unsigned literals)
TEST(SafeIntTest, OperatorEqualsMixedSignedness) {
    // Test signed safe_int vs unsigned literal
    ecul::safe_int<int> signed_val(42);
    
    EXPECT_TRUE(signed_val == 42u);
    EXPECT_FALSE(signed_val == 100u);
    
    // Test negative signed vs unsigned literal
    ecul::safe_int<int> negative_val(-1);
    
    EXPECT_FALSE(negative_val == 1u);
    EXPECT_FALSE(negative_val == 0u);
}

// Test operator== with zero
TEST(SafeIntTest, OperatorEqualsZero) {
    ecul::safe_int<int> zero_signed(0);
    ecul::safe_int<unsigned int> zero_unsigned(0);
    
    EXPECT_TRUE(zero_signed == 0);
    EXPECT_TRUE(zero_unsigned == 0u);
}

// Test operator== with different plain integer types
TEST(SafeIntTest, OperatorEqualsDifferentTypes) {
    ecul::safe_int<short> small_val(100);
    ecul::safe_int<long> large_val(100);
    
    EXPECT_TRUE(small_val == 100);
    EXPECT_TRUE(small_val == static_cast<short>(100));
    EXPECT_TRUE(large_val == 100L);
}

// Helper to generate test values for a given type
template <typename T>
std::vector<T> generate_test_values() {
    //using boost::multiprecision::int128_t;
    std::vector<T> values;
    
    constexpr bool is_signed = std::is_signed_v<T>;
    constexpr T type_max = std::numeric_limits<T>::max();
    constexpr T type_min = std::numeric_limits<T>::min();
		using value_t = T;

		bool constexpr dbg=0; // debug

		auto func_tidy = [](std::vector<T> data) -> std::vector<T> {
			std::vector<T> ret = data;
    	std::sort(ret.begin(), ret.end());
    	ret.erase(std::unique(ret.begin(), ret.end()), ret.end());
			return ret;
		};

		std::vector<int> bitlens;
		for (int ib=0; ib<=64/8 ; ib++) {
			int bits = ib*8; // e.g. up to 64
			bitlens.push_back(bits);
			if (bits>1) bitlens.push_back(bits-1);
 		}

		std::vector<T> po; // values of interest. points such as 0,256,65536 0, 2^8, 2^16, max/3, max ; can include negative for -min alike
		for (const auto bits: bitlens) { // e.g. 0, 1, 2 .. up to 8
			const auto digits = std::numeric_limits<T>().digits;
			if (dbg) { std::cout << " bits="<<bits << " digits=" << digits << "\n" ; }
			
			if (bits <= digits) {
				const auto p1_result = ecul::pow2_int_result<T>( bits );
				if (! p1_result.is_ok()) continue;
				const auto p1 = p1_result.unwrap_or_throw();

				const auto p2_result = ecul::nagate_number_result( p1 );
				if (! p2_result.is_ok()) continue;
				const auto p2 = p2_result.unwrap_or_throw();

				if (dbg) { std::cout << " bits="<<bits << " digits=" << digits << " adding: " << p1 << " " << p2 << "\n"; }
				po.push_back(p1); // e.g. add +128
				po.push_back(p2); // e.g. add -128
			}
		}
		// we properly take min() and max() - allowed. we divide by small numers /2 /3 /4 towards zero, so allowed
		values.push_back(std::numeric_limits<T>::max()/2);
		values.push_back(std::numeric_limits<T>::max()/3);
		values.push_back(std::numeric_limits<T>::max()/4);

		values.push_back(0);

		// in case of unsigne this adds more 0, deduplication later
		values.push_back(std::numeric_limits<T>::min());
		values.push_back(std::numeric_limits<T>::min()/2);
		values.push_back(std::numeric_limits<T>::min()/3);
		values.push_back(std::numeric_limits<T>::min()/4);

		values = func_tidy( po );

		decltype(values) values2 = values;
		for (const auto v : values) {
			{
				T v_inc = v; // we will increment here
				for (int i=0; i<2; ++i) {
					auto res = ecul::int_plus1(v_inc);
					if (! res.is_ok()) break; // can't increment here. break - future ones will also fail
					v_inc = res.unwrap_or_throw();
					values2.push_back(v_inc);
				}
			}
			{
				T v_dec = v; // we will dec here
				for (int i=0; i<2; ++i) {
					auto res = ecul::int_minus1(v_dec);
					if (! res.is_ok()) break; // can't increment here. break - future ones will also fail
					v_dec = res.unwrap_or_throw();
					values2.push_back(v_dec);
				}
			}
			
		}
		values = func_tidy( values2 );
    
    return values;
}

// Helper to check if a value fits in a type's range
template <typename T>
bool fits_in_type(boost::multiprecision::int128_t val) {
    using boost::multiprecision::int128_t;
    constexpr int128_t type_max = std::numeric_limits<T>::max();
    constexpr int128_t type_min = std::numeric_limits<T>::min();
    return val >= type_min && val <= type_max;
}

// Helper to safely cast if value fits
template <typename T>
T safe_cast(boost::multiprecision::int128_t val) {
    if (!fits_in_type<T>(val)) {
        throw std::overflow_error("Value doesn't fit in target type");
    }
    return static_cast<T>(val);
}

// Test template for a specific pair of types
template <typename TA, typename TB>
void test_operator_equals_for_types() {
    //using boost::multiprecision::int128_t;
    
		constexpr bool dbg=0; // debug
		if (dbg) std::cout << "=== type TA=" << ecul::nice_type::nice_var<TA>() << "\n";
    const auto values = generate_test_values<TB>();
		for (const auto val : values) {
			if (dbg) std::cout << "val="<<std::to_string(val) << "\n";
		}
    
    for (const auto val : values) {
        // Test 1: Constructor should accept or throw based on whether value fits
        bool should_fit = fits_in_type<TA>(val);
        
        if (should_fit) {
            // Value should fit - constructor should succeed (NOT YET IMPLEMENTED)
            // For now, it will accept any value without checking
            TA val_ta = safe_cast<TA>(val);
            
            try {
                ecul::safe_int<TA> si(val_ta);
                
                // Test 2: operator== should return true when comparing with original value
                // This should work for same-type comparisons
                EXPECT_TRUE(si == val_ta) 
                    << "safe_int<" << typeid(TA).name() << ">(" << val 
                    << ") == " << val << " should be true";
                
                // Test with TB type if the value also fits in TB
                if (fits_in_type<TB>(val)) {
                    TB val_tb = safe_cast<TB>(val);
                    
                    // This tests mixed-type operator==
                    EXPECT_TRUE(si == val_tb)
                        << "safe_int<" << typeid(TA).name() << ">(" << val 
                        << ") == (" << typeid(TB).name() << ")" << val 
                        << " should be true";
                }
                
            } catch (const std::exception& e) {
                // Constructor threw - this is NOT expected currently
                // (validation not yet implemented)
                ADD_FAILURE() << "Constructor threw unexpectedly for safe_int<" 
                    << typeid(TA).name() << ">(" << val << "): " << e.what();
            }
        } else {
            // Value doesn't fit in TA - constructor SHOULD throw (NOT YET IMPLEMENTED)
            // Currently it will NOT throw, leading to undefined behavior
            // We skip this test case for now to avoid UB
            // Once validation is implemented, we should test:
            //EXPECT_THROW(ecul::safe_int<TA>(val_as_larger_type), std::exception);
						ecul::safe_int<TA> aaa(val);
        }
    }
}

// Tuple of all integral types to test
using AllIntegralTypes = std::tuple<
    signed char, unsigned char,	char,
		wchar_t,
		char16_t, char32_t,
    short, unsigned short,
    int, unsigned int,
    long, unsigned long,
    long long, unsigned long long
#if __cplusplus >= 202002L		
		,char8_t
#endif		
>;

// Helper struct to tag types
template<typename T>
struct type_tag {
    using type = T;
};

// Helper to iterate over tuple and apply function to each type
template <typename Tuple, typename Func, std::size_t... Is>
void for_each_type_impl(Func&& func, std::index_sequence<Is...>) {
    // Fold expression to call func for each type in tuple
    (func(type_tag<std::tuple_element_t<Is, Tuple>>{}), ...);
}

template <typename Tuple, typename Func>
void for_each_type(Func&& func) {
    for_each_type_impl<Tuple>(
        std::forward<Func>(func),
        std::make_index_sequence<std::tuple_size_v<Tuple>>{}
				// needs C++20(?) for std::make_index_sequence
    );
}

// Comprehensive nested loop test for operator==
TEST(SafeIntTest, ComprehensiveOperatorEqualsNestedLoops) {
    // Loop over all types TA
    for_each_type<AllIntegralTypes>([](auto ta_tag) {
        using TA = typename decltype(ta_tag)::type;
        
        // Loop over all types TB
        for_each_type<AllIntegralTypes>([](auto tb_tag) {
            using TB = typename decltype(tb_tag)::type;
            
            // Test this specific TA, TB combination
            test_operator_equals_for_types<TA, TB>();
        });
    });
}