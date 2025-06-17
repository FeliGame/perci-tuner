// #include <cmath>
// #include <limits>
//  #include <stdio.h>
#include "dd_real.h"
#include "inline.h"

#define funprefix extern
// #define funprefix

/*********** Basic Functions ************/
/* Computes fl(a+b) and err(a+b).  Assumes |a| >= |b|. */
funprefix inline double quick_two_sum(double a, double b, double *err)
{
	double s = a + b;
	*err = b - (s - a);
	return s;
}

/* Computes fl(a-b) and err(a-b).  Assumes |a| >= |b| */
funprefix inline double quick_two_diff(double a, double b, double *err)
{
	double s = a - b;
	*err = (a - s) - b;
	return s;
}

/* Computes fl(a+b) and err(a+b).  */
funprefix inline double two_sum(double a, double b, double *err)
{
	double s = a + b;
	double bb = s - a;
	*err = (a - (s - bb)) + (b - bb);
	return s;
}

/* Computes fl(a-b) and err(a-b).  */
funprefix inline double two_diff(double a, double b, double *err)
{
	double s = a - b;
	double bb = s - a;
	*err = (a - (s - bb)) - (b + bb);
	return s;
}

funprefix inline void split(double a, double *hi, double *lo)
{
	double temp;
	if (a > _QD_SPLIT_THRESH || a < -_QD_SPLIT_THRESH)
	{
		a *= 3.7252902984619140625e-09; // 2^-28
		temp = _QD_SPLITTER * a;
		*hi = temp - (temp - a);
		*lo = a - *hi;
		*hi *= 268435456.0; // 2^28
		*lo *= 268435456.0; // 2^28
	}
	else
	{
		temp = _QD_SPLITTER * a;
		*hi = temp - (temp - a);
		*lo = a - *hi;
	}
}

/* Computes fl(a*b) and err(a*b). */
funprefix inline double two_prod(double a, double b, double *err)
{
	// double a_hi, a_lo, b_hi, b_lo;
	double p = a * b;
	// split(a, &a_hi, &a_lo);
	// split(b, &b_hi, &b_lo);
	//*err = ((a_hi * b_hi - p) + a_hi * b_lo + a_lo * b_hi) + a_lo * b_lo;
	*err = fma(a, b, -p);

	return p;
}

/* Computes fl(a*a) and err(a*a).  Faster than the above method. */
funprefix inline double two_sqr(double a, double *err)
{
	double hi, lo;
	double q = a * a;
	split(a, &hi, &lo);
	*err = ((hi * hi - q) + 2.0 * hi * lo) + lo * lo;
	return q;
}

/* init */
funprefix inline dd_real dd_init(const double cx)
{
	dd_real t;
	t.x[0] = cx;
	t.x[1] = 0.0;
	// printf("in dd_init x=%le, y=%le\n", t.x, t.y);
	// PRINTDD("dd_init", t.x, t.y);
	return t;
}

funprefix inline dd_real dd_init2(const double cx1, const double cx2)
{
	dd_real t;
	t.x[0] = cx1;
	t.x[1] = cx2;
	return t;
}

/*********** Additions ************/
/* double-double = double + double */
funprefix inline dd_real add_d_d(double a, double b)
{
	double s, e;
	s = two_sum(a, b, &e);
	return dd_init2(s, e);
}

/* double-double + double */
funprefix inline dd_real add_dd_d(const dd_real a, double b)
{
	double s1, s2;
	s1 = two_sum(a.x[0], b, &s2);
	s2 += a.x[1];
	s1 = quick_two_sum(s1, s2, &s2);
	return dd_init2(s1, s2);
}

funprefix inline dd_real ieee_add(const dd_real a, const dd_real b)
{
	// PRINTDD("add_dd_dd", a.x, b.x);
	// PRINTDD("add_dd_dd", a.y, b.y);
	double s1, s2, t1, t2;
	s1 = two_sum(a.x[0], b.x[0], &s2);
	t1 = two_sum(a.x[1], b.x[1], &t2);
	s2 += t1;
	s1 = quick_two_sum(s1, s2, &s2);
	s2 += t2;
	s1 = quick_two_sum(s1, s2, &s2);
	// PRINTDD("add_dd_dd", s1, s2);
	return dd_init2(s1, s2);
}

funprefix inline dd_real sloppy_add(const dd_real a, const dd_real b)
{
	double s, e;

	s = two_sum(a.x[0], b.x[0], &e);
	e += (a.x[1] + b.x[1]);

	s = quick_two_sum(s, e, &e);
	return dd_init2(s, e);
}

/* double-double + double-double */
funprefix inline dd_real add_dd_dd(const dd_real a, const dd_real b)
{
#ifndef IEEE_ADD
	return sloppy_add(a, b);
#else
	return ieee_add(a, b);
#endif
}

/* double + double-double */
funprefix inline dd_real add_d_dd(double a, const dd_real b)
{
	return add_dd_d(b, a);
}

/*********** Subtractions ************/
/* double-double = double - double */
funprefix inline dd_real sub_d_d(double a, double b)
{
	double s, e;
	s = two_diff(a, b, &e);
	return dd_init2(s, e);
}

/* double-double - double */
funprefix inline dd_real sub_dd_d(const dd_real a, double b)
{
	double s1, s2;
	s1 = two_diff(a.x[0], b, &s2);
	s2 += a.x[1];
	s1 = quick_two_sum(s1, s2, &s2);
	return dd_init2(s1, s2);
}

/* double-double - double-double */
funprefix inline dd_real sub_dd_dd(const dd_real a, const dd_real b)
{
#ifndef IEEE_ADD
	double s, e;
	s = two_diff(a.x[0], b.x[0], &e);
	e += a.x[1];
	e -= b.x[1];
	s = quick_two_sum(s, e, &e);
	return dd_init2(s, e);
#else
	double s1, s2, t1, t2;
	s1 = two_diff(a.x[0], b.x[0], &s2);
	t1 = two_diff(a.x[1], b.x[1], &t2);
	s2 += t1;
	s1 = quick_two_sum(s1, s2, &s2);
	s2 += t2;
	s1 = quick_two_sum(s1, s2, &s2);
	return dd_init2(s1, s2);
#endif
}

/* double - double-double */
funprefix inline dd_real sub_d_dd(double a, const dd_real b)
{
	double s1, s2;
	s1 = two_diff(a, b.x[0], &s2);
	s2 -= b.x[1];
	s1 = quick_two_sum(s1, s2, &s2);
	return dd_init2(s1, s2);
}

/*********** Unary Minus ***********/
funprefix inline dd_real neg_dd(const dd_real a)
{
	return dd_init2(-a.x[0], -a.x[1]);
}

/*********** Multiplications ************/
/* double-double = double * double */
funprefix inline dd_real mul_d_d(double a, double b)
{
	double p, e;
	p = two_prod(a, b, &e);
	return dd_init2(p, e);
}

/* double-double * (2.0 ^ exp) */
funprefix inline dd_real ldexp_dd_i(const dd_real a, int exp)
{
	return dd_init2(ldexp(a.x[0], exp), ldexp(a.x[1], exp));
}

/* double-double * double,  where double is a power of 2. */
funprefix inline dd_real mul_pwr2_dd_d(const dd_real a, double b)
{
	return dd_init2(a.x[0] * b, a.x[1] * b);
}

/* double-double * double */
funprefix inline dd_real mul_dd_d(const dd_real a, double b)
{
	double p1, p2;

	p1 = two_prod(a.x[0], b, &p2);
	p2 += (a.x[1] * b);
	p1 = quick_two_sum(p1, p2, &p2);
	return dd_init2(p1, p2);
}

/* double-double * double-double */
funprefix inline dd_real mul_dd_dd(const dd_real a, const dd_real b)
{
	// PRINTDD("mul_dd_dd", a.x, b.x);
	// PRINTDD("mul_dd_dd", a.y, b.y);
	double p1, p2;

	p1 = two_prod(a.x[0], b.x[0], &p2);
	p2 += (a.x[0] * b.x[1] + a.x[1] * b.x[0]);
	p1 = quick_two_sum(p1, p2, &p2);
	// PRINTDD("mul_dd_dd", p1, p2);
	return dd_init2(p1, p2);
}

/* double * double-double */
funprefix inline dd_real mul_d_dd(double a, const dd_real b)
{
	return mul_dd_d(b, a);
}

/*********** Divisions ************/
/* double-double = double / double */
funprefix inline dd_real div_d_d(double a, double b)
{
	double q1, q2;
	double p1, p2;
	double s, e;

	q1 = a / b;

	/* Compute  a - q1 * b */
	p1 = two_prod(q1, b, &p2);
	s = two_diff(a, p1, &e);
	e -= p2;

	/* get next approximation */
	q2 = (s + e) / b;

	s = quick_two_sum(q1, q2, &e);

	return dd_init2(s, e);
}

/* double-double / double */
funprefix inline dd_real div_dd_d(const dd_real a, double b)
{
	double q1, q2;
	double p1, p2;
	double s, e;
	dd_real r;

	q1 = a.x[0] / b; /* approximate quotient. */

	/* Compute  this - q1 * d */
	p1 = two_prod(q1, b, &p2);
	s = two_diff(a.x[0], p1, &e);
	e += a.x[1];
	e -= p2;

	/* get next approximation. */
	q2 = (s + e) / b;

	/* renormalize */
	r.x[0] = quick_two_sum(q1, q2, &r.x[1]);

	return r;
}

/* double-double / double-double */
funprefix inline dd_real div_dd_dd(const dd_real a, const dd_real b)
{
	double q1, q2, q3;
	dd_real r;

	q1 = a.x[0] / b.x[0]; /* approximate quotient */

	// r = a - q1 * b;
	r = sub_dd_dd(a, mul_dd_d(b, q1));

	q2 = r.x[0] / b.x[0];
	// r -= (q2 * b);
	r = sub_dd_dd(r, mul_dd_d(b, q2));

	q3 = r.x[0] / b.x[0];

	q1 = quick_two_sum(q1, q2, &q2);
	r = add_dd_d(dd_init2(q1, q2), q3);
	return r;
}

/* double / double-double */
funprefix inline dd_real div_d_dd(double a, const dd_real b)
{
	return div_dd_dd(dd_init(a), b);
}

/* invert */
funprefix inline dd_real inv_dd(const dd_real a)
{
	return div_d_dd(1.0, a);
}

/*********** Squaring **********/
funprefix inline dd_real sqr_dd(const dd_real a)
{
	double p1, p2;
	double s1, s2;
	p1 = two_sqr(a.x[0], &p2);
	p2 += 2.0 * a.x[0] * a.x[1];
	p2 += a.x[1] * a.x[1];
	s1 = quick_two_sum(p1, p2, &s2);
	return dd_init2(s1, s2);
}

funprefix inline dd_real sqr_d(double a)
{
	double p1, p2;
	p1 = two_sqr(a, &p2);
	return dd_init2(p1, p2);
}

/*********** Micellaneous ************/
/*  this == 0 */
funprefix inline bool is_zero(const dd_real a)
{
	return (a.x[0] == 0.0);
}

/*  this == 1 */
funprefix inline bool is_one(const dd_real a)
{
	return (a.x[0] == 1.0 && a.x[1] == 0.0);
}

/*  this > 0 */
funprefix inline bool is_positive(const dd_real a)
{
	return (a.x[0] > 0.0);
}

/* this < 0 */
funprefix inline bool is_negative(const dd_real a)
{
	return (a.x[0] < 0.0);
}

/* Computes the n-th power of a double-double number.
   NOTE:  0^0 causes an error.                         */
funprefix inline dd_real npwr_dd_i(dd_real a, int n)
{
	if (n == 0)
	{
		if (is_zero(a))
		{
			fprintf(stderr, "(dd_real::npwr): Invalid argument.");
			return DD_NAN;
		}
		return dd_init(1.0);
	}

	dd_real r = a;
	dd_real s = dd_init(1.0);
	int N = abs(n);

	if (N > 1)
	{
		/* Use binary exponentiation */
		while (N > 0)
		{
			if (N % 2 == 1)
			{
				s = mul_dd_dd(s, r);
			}
			N /= 2;
			if (N > 0)
			{
				r = sqr_dd(r);
			}
		}
	}
	else
	{
		s = r;
	}

	/* Compute the reciprocal if n is negative. */
	if (n < 0)
		return div_d_dd(1.0, s);

	return s;
}

/*********** Equality Comparisons ************/
/* double-double == double */
funprefix inline bool eq_dd_d(const dd_real a, double b)
{
	return (a.x[0] == b && a.x[1] == 0.0);
}

/* double-double == double-double */
funprefix inline bool eq_dd_dd(const dd_real a, const dd_real b)
{
	return (a.x[0] == b.x[0] && a.x[1] == b.x[1]);
}

/* double == double-double */
funprefix inline bool eq_d_dd(double a, const dd_real b)
{
	return (a == b.x[0] && b.x[1] == 0.0);
}

/*********** Greater-Than Comparisons ************/
/* double-double > double */
funprefix inline bool gt_dd_d(const dd_real a, double b)
{
	return (a.x[0] > b || (a.x[0] == b && a.x[1] > 0.0));
}

/* double-double > double-double */
funprefix inline bool gt_dd_dd(const dd_real a, const dd_real b)
{
	return (a.x[0] > b.x[0] || (a.x[0] == b.x[0] && a.x[1] > b.x[1]));
}

/* double > double-double */
funprefix inline bool gt_d_dd(double a, const dd_real b)
{
	return (a > b.x[0] || (a == b.x[0] && b.x[1] < 0.0));
}

/*********** Less-Than Comparisons ************/
/* double-double < double */
funprefix inline bool lt_dd_d(const dd_real a, double b)
{
	return (a.x[0] < b || (a.x[0] == b && a.x[1] < 0.0));
}

/* double-double < double-double */
funprefix inline bool lt_dd_dd(const dd_real a, const dd_real b)
{
	return (a.x[0] < b.x[0] || (a.x[0] == b.x[0] && a.x[1] < b.x[1]));
}

/* double < double-double */
funprefix inline bool lt_d_dd(double a, const dd_real b)
{
	return (a < b.x[0] || (a == b.x[0] && b.x[1] > 0.0));
}

/*********** Greater-Than-Or-Equal-To Comparisons ************/
/* double-double >= double */
funprefix inline bool ge_dd_d(const dd_real a, double b)
{
	return (a.x[0] > b || (a.x[0] == b && a.x[1] >= 0.0));
}

/* double-double >= double-double */
funprefix inline bool ge_dd_dd(const dd_real a, const dd_real b)
{
	return (a.x[0] > b.x[0] || (a.x[0] == b.x[0] && a.x[1] >= b.x[1]));
}

/* double >= double-double */
funprefix inline bool ge_d_dd(double a, const dd_real b)
{
	return (b.x[0] < a || (b.x[0] == a && b.x[1] < 0.0));
}

/*********** Less-Than-Or-Equal-To Comparisons ************/
/* double-double <= double */
funprefix inline bool le_dd_d(const dd_real a, double b)
{
	return (a.x[0] < b || (a.x[0] == b && a.x[1] <= 0.0));
}

/* double-double <= double-double */
funprefix inline bool le_dd_dd(const dd_real a, const dd_real b)
{
	return (a.x[0] < b.x[0] || (a.x[0] == b.x[0] && a.x[1] <= b.x[1]));
}

/* double <= double-double */
funprefix inline bool le_d_dd(double a, const dd_real b)
{
	return (b.x[0] > a || (b.x[0] == a && b.x[1] >= 0.0));
}

/*********** Not-Equal-To Comparisons ************/
/* double-double != double */
funprefix inline bool ne_dd_d(const dd_real a, double b)
{
	return (a.x[0] != b || a.x[1] != 0.0);
}

/* double-double != double-double */
funprefix inline bool ne_dd_dd(const dd_real a, const dd_real b)
{
	return (a.x[0] != b.x[0] || a.x[1] != b.x[1]);
}

/* double != double-double */
funprefix inline bool ne_d_dd(double a, const dd_real b)
{
	return (a != b.x[0] || b.x[1] != 0.0);
}

/* Absolute value */
funprefix inline dd_real abs_dd(const dd_real a)
{
	dd_real t = a;
	if (a.x[0] < 0.0)
	{
		t.x[0] *= -1;
		t.x[1] *= -1;
	}
	return t;
}

funprefix inline dd_real fabs_dd(const dd_real a)
{
	return abs_dd(a);
}

/* Computes the nearest integer to d. */
funprefix inline double nint(double d)
{
	if (d == floor(d))
		return d;
	return floor(d + 0.5);
}

/* Round to Nearest integer */
funprefix inline dd_real nint_dd(const dd_real a)
{
	double hi = nint(a.x[0]);
	double lo;

	if (hi == a.x[0])
	{
		/* High word is an integer already.  Round the low word.*/
		lo = nint(a.x[1]);

		/* Renormalize. This is needed if x[0] = some integer, x[1] = 1/2.*/
		hi = quick_two_sum(hi, lo, &lo);
	}
	else
	{
		/* High word is not an integer. */
		lo = 0.0;
		if (fabs(hi - a.x[0]) == 0.5 && a.x[1] < 0.0)
		{
			/* There is a tie in the high word, consult the low word
			   to break the tie. */
			hi -= 1.0; /* NOTE: This does not cause INEXACT. */
		}
	}

	return dd_init2(hi, lo);
}

funprefix inline dd_real floor_dd(const dd_real a)
{
	double hi = floor(a.x[0]);
	double lo = 0.0;

	if (hi == a.x[0])
	{
		/* High word is integer already.  Round the low word. */
		lo = floor(a.x[1]);
		hi = quick_two_sum(hi, lo, &lo);
	}

	return dd_init2(hi, lo);
}

funprefix inline dd_real ceil_dd(const dd_real a)
{
	double hi = ceil(a.x[0]);
	double lo = 0.0;

	if (hi == a.x[0])
	{
		/* High word is integer already.  Round the low word. */
		lo = ceil(a.x[1]);
		hi = quick_two_sum(hi, lo, &lo);
	}

	return dd_init2(hi, lo);
}

funprefix inline dd_real aint_dd(const dd_real a)
{
	return (a.x[0] >= 0.0) ? floor_dd(a) : ceil_dd(a);
}

/********** Remainder **********/
funprefix inline dd_real drem_dd_dd(const dd_real a, const dd_real b)
{
	dd_real n = nint_dd(div_dd_dd(a, b));
	return sub_dd_dd(a, mul_dd_dd(n, b));
}

funprefix inline dd_real divrem_dd_dd(const dd_real a, const dd_real b, dd_real *r)
{
	dd_real n = nint_dd(div_dd_dd(a, b));
	*r = sub_dd_dd(a, mul_dd_dd(n, b));
	return n;
}

/* Exponential.  Computes exp(x) in double-double precision. */
funprefix inline dd_real exp_dd(const dd_real a)
{
	/* Strategy:  We first reduce the size of x by noting that

			exp(kr + m * log(2)) = 2^m * exp(r)^k

	   where m and k are integers.  By choosing m appropriately
	   we can make |kr| <= log(2) / 2 = 0.347.  Then exp(r) is
	   evaluated using the familiar Taylor series.  Reducing the
	   argument substantially speeds up the convergence.       */

	const double k = 512.0;
	const double inv_k = 1.0 / k;

	if (a.x[0] <= -709.0)
		return dd_init(0.0);

	if (a.x[0] >= 709.0)
		return DD_INF;

	if (is_zero(a))
		return dd_init(1.0);

	if (is_one(a))
		return DD_E;

	double m = floor(a.x[0] / DD_LOG2.x[0] + 0.5);
	dd_real r = mul_pwr2_dd_d(sub_dd_dd(a, mul_dd_d(DD_LOG2, m)), inv_k);
	dd_real s, t, p;

	p = sqr_dd(r);
	s = add_dd_dd(r, mul_pwr2_dd_d(p, 0.5));
	p = mul_dd_dd(p, r);
	t = mul_dd_dd(p, dd_init2(inv_fact[0][0], inv_fact[0][1]));
	int i = 0;
	do
	{
		s = add_dd_dd(s, t);
		p = mul_dd_dd(p, r);
		++i;
		t = mul_dd_dd(p, dd_init2(inv_fact[i][0], inv_fact[i][1]));
	} while (fabs(t.x[0]) > (inv_k * DD_EPS) && i < 5);

	s = add_dd_dd(s, t);

	s = add_dd_dd(mul_pwr2_dd_d(s, 2.0), sqr_dd(s));
	s = add_dd_dd(mul_pwr2_dd_d(s, 2.0), sqr_dd(s));
	s = add_dd_dd(mul_pwr2_dd_d(s, 2.0), sqr_dd(s));
	s = add_dd_dd(mul_pwr2_dd_d(s, 2.0), sqr_dd(s));
	s = add_dd_dd(mul_pwr2_dd_d(s, 2.0), sqr_dd(s));
	s = add_dd_dd(mul_pwr2_dd_d(s, 2.0), sqr_dd(s));
	s = add_dd_dd(mul_pwr2_dd_d(s, 2.0), sqr_dd(s));
	s = add_dd_dd(mul_pwr2_dd_d(s, 2.0), sqr_dd(s));
	s = add_dd_dd(mul_pwr2_dd_d(s, 2.0), sqr_dd(s));
	s = add_dd_d(s, 1.0);

	return ldexp_dd_i(s, (int)(m));
}

/* Logarithm.  Computes log(x) in double-double precision.
   This is a natural logarithm (i.e., base e).            */
funprefix inline dd_real log_dd(const dd_real a)
{
	/* Strategy.  The Taylor series for log converges much more
	   slowly than that of exp, due to the lack of the factorial
	   term in the denominator.  Hence this routine instead tries
	   to determine the root of the function

		   f(x) = exp(x) - a

	   using Newton iteration.  The iteration is given by

		   x' = x - f(x)/f'(x)
			  = x - (1 - a * exp(-x))
			  = x + a * exp(-x) - 1.

	   Only one iteration is needed, since Newton's iteration
	   approximately doubles the number of digits per iteration. */

	if (is_one(a))
	{
		return dd_init(0.0);
	}

	if (a.x[0] <= 0.0)
	{
		fprintf(stderr, "(dd_real::log): Non-positive argument.");
		return DD_NAN;
	}

	dd_real x = dd_init(log(a.x[0])); /* Initial approximation */

	x = sub_dd_d(add_dd_dd(x, mul_dd_dd(a, exp_dd(neg_dd(x)))), 1.0);
	return x;
}

funprefix inline dd_real log10_dd(const dd_real a)
{
	return div_dd_dd(log_dd(a), DD_LOG10);
}

funprefix inline dd_real pow_dd_i(const dd_real a, int n)
{
	return npwr_dd_i(a, n);
}

funprefix inline dd_real pow_dd_dd(const dd_real a, const dd_real b)
{
	return exp_dd(mul_dd_dd(b, log_dd(a)));
}

/* Computes the square root of the double-double number dd.
	NOTE: dd must be a non-negative number.                   */
funprefix inline dd_real sqrt_dd(const dd_real a)
{
	/* Strategy:  Use Karp's trick:  if x is an approximation
		to sqrt(a), then

			sqrt(a) = a*x + [a - (a*x)^2] * x / 2   (approx)

		The approximation is accurate to twice the accuracy of x.
		Also, the multiplication (a*x) and [-]*x can be done with
		only half the precision.
	*/

	if (is_zero(a))
		return dd_init(0.0);

	if (is_negative(a))
	{
		fprintf(stderr, "(dd_real::sqrt): Negative argument.");
		return DD_NAN;
	}

	double x = 1.0 / sqrt(a.x[0]);
	double ax = a.x[0] * x;
	dd_real tmp = sub_dd_dd(a, sqr_d(ax));
	return add_d_d(ax, tmp.x[0] * (x * 0.5));
}

/* Computes the n-th root of the double-double number a.
	NOTE: n must be a positive integer.
	NOTE: If n is even, then a must not be negative.       */
funprefix inline dd_real nroot_dd_i(const dd_real a, int n)
{
	/* Strategy:  Use Newton iteration for the function

			f(x) = x^(-n) - a

		to find its root a^{-1/n}.  The iteration is thus

			x' = x + x * (1 - a * x^n) / n

		which converges quadratically.  We can then find
		a^{1/n} by taking the reciprocal.
	*/

	if (n <= 0)
	{
		fprintf(stderr, "(dd_real::nroot): N must be positive.");
		return DD_NAN;
	}

	if (n % 2 == 0 && is_negative(a))
	{
		fprintf(stderr, "(dd_real::nroot): Negative argument.");
		return DD_NAN;
	}

	if (n == 1)
	{
		return a;
	}
	if (n == 2)
	{
		return sqrt_dd(a);
	}

	if (is_zero(a))
		return dd_init(0.0);

	/* Note  a^{-1/n} = exp(-log(a)/n) */
	dd_real r = abs_dd(a);
	dd_real x = dd_init(exp(-log(r.x[0]) / n));

	/* Perform Newton's iteration. */
	x = add_dd_dd(x, div_dd_d(mul_dd_dd(x, sub_d_dd(1.0,
													mul_dd_dd(r, npwr_dd_i(x, n)))),
							  (double)n));

	if (a.x[0] < 0.0)
	{
		x.x[0] *= -1;
		x.x[1] *= -1;
	}

	return inv_dd(x);
}

/* Computes sin(a) using Taylor series.
   Assumes |a| <= pi/32.                           */
static dd_real sin_taylor(const dd_real a)
{
	const double thresh = 0.5 * fabs(a.x[0]) * DD_EPS;
	dd_real r, s, t, x;

	if (is_zero(a))
	{
		return dd_init(0.0);
	}

	int i = 0;
	x = neg_dd(sqr_dd(a));
	s = a;
	r = a;
	do
	{
		r = mul_dd_dd(r, x);
		t = mul_dd_dd(r, dd_init2(inv_fact[i][0], inv_fact[i][1]));
		s = add_dd_dd(s, t);
		i += 2;
	} while (i < n_inv_fact && fabs(t.x[0]) > thresh);

	return s;
}

static dd_real cos_taylor(const dd_real a)
{
	const double thresh = 0.5 * DD_EPS;
	dd_real r, s, t, x;

	if (is_zero(a))
	{
		return dd_init(1.0);
	}

	x = neg_dd(sqr_dd(a));
	r = x;
	s = add_d_dd(1.0, mul_pwr2_dd_d(r, 0.5));
	int i = 1;
	do
	{
		r = mul_dd_dd(r, x);
		t = mul_dd_dd(r, dd_init2(inv_fact[i][0], inv_fact[i][1]));
		s = add_dd_dd(s, t);
		i += 2;
	} while (i < n_inv_fact && fabs(t.x[0]) > thresh);

	return s;
}

static void sincos_taylor(const dd_real a,
						  dd_real *sin_a, dd_real *cos_a)
{
	if (is_zero(a))
	{
		*sin_a = dd_init(0.0);
		*cos_a = dd_init(1.0);
		return;
	}

	*sin_a = sin_taylor(a);

	*cos_a = sqrt_dd(sub_d_dd(1.0, sqr_dd(*sin_a)));
}

dd_real sin_dd(const dd_real a)
{

	/* Strategy.  To compute sin(x), we choose integers a, b so that

		 x = s + a * (pi/2) + b * (pi/16)

	   and |s| <= pi/32.  Using the fact that

		 sin(pi/16) = 0.5 * sqrt(2 - sqrt(2 + sqrt(2)))

	   we can compute sin(x) from sin(s), cos(s).  This greatly
	   increases the convergence of the sine Taylor series. */

	if (is_zero(a))
	{
		return dd_init(0.0);
	}

	// approximately reduce modulo 2*pi
	dd_real z = nint_dd(div_dd_dd(a, DD_2PI));
	dd_real r = sub_dd_dd(a, mul_dd_dd(DD_2PI, z));

	// approximately reduce modulo pi/2 and then modulo pi/16.
	dd_real t;
	double q = floor(r.x[0] / DD_PI2.x[0] + 0.5);
	t = sub_dd_dd(r, mul_dd_d(DD_PI2, q));
	int j = (int)(q);
	q = floor(t.x[0] / _pi16.x[0] + 0.5);
	t = sub_dd_dd(t, mul_dd_d(_pi16, q));
	int k = (int)(q);
	int abs_k = abs(k);

	if (j < -2 || j > 2)
	{
		fprintf(stderr, "(dd_real::sin): Cannot reduce modulo pi/2.");
		return DD_NAN;
	}

	if (abs_k > 4)
	{
		fprintf(stderr, "(dd_real::sin): Cannot reduce modulo pi/16.");
		return DD_NAN;
	}

	if (k == 0)
	{
		switch (j)
		{
		case 0:
			return sin_taylor(t);
		case 1:
			return cos_taylor(t);
		case -1:
			return neg_dd(cos_taylor(t));
		default:
			return neg_dd(sin_taylor(t));
		}
	}

	dd_real u = dd_init2(cos_table[abs_k - 1][0], cos_table[abs_k - 1][1]);
	dd_real v = dd_init2(sin_table[abs_k - 1][0], sin_table[abs_k - 1][1]);
	dd_real sin_t, cos_t;
	sincos_taylor(t, &sin_t, &cos_t);
	if (j == 0)
	{
		if (k > 0)
		{
			r = add_dd_dd(mul_dd_dd(u, sin_t), mul_dd_dd(v, cos_t));
		}
		else
		{
			r = sub_dd_dd(mul_dd_dd(u, sin_t), mul_dd_dd(v, cos_t));
		}
	}
	else if (j == 1)
	{
		if (k > 0)
		{
			r = sub_dd_dd(mul_dd_dd(u, cos_t), mul_dd_dd(v, sin_t));
		}
		else
		{
			r = add_dd_dd(mul_dd_dd(u, cos_t), mul_dd_dd(v, sin_t));
		}
	}
	else if (j == -1)
	{
		if (k > 0)
		{
			r = sub_dd_dd(mul_dd_dd(v, sin_t), mul_dd_dd(u, cos_t));
		}
		else if (k < 0)
		{
			r = neg_dd(add_dd_dd(mul_dd_dd(u, cos_t), mul_dd_dd(v, sin_t)));
		}
	}
	else
	{
		if (k > 0)
		{
			r = neg_dd(add_dd_dd(mul_dd_dd(u, sin_t), mul_dd_dd(v, cos_t)));
		}
		else
		{
			r = sub_dd_dd(mul_dd_dd(v, cos_t), mul_dd_dd(u, sin_t));
		}
	}

	return r;
}

dd_real cos_dd(const dd_real a)
{

	if (is_zero(a))
	{
		return dd_init(1.0);
	}

	// approximately reduce modulo 2*pi
	dd_real z = nint_dd(div_dd_dd(a, DD_2PI));
	dd_real r = sub_dd_dd(a, mul_dd_dd(z, DD_2PI));

	// approximately reduce modulo pi/2 and then modulo pi/16
	dd_real t;
	double q = floor(r.x[0] / DD_PI2.x[0] + 0.5);
	t = sub_dd_dd(r, mul_dd_d(DD_PI2, q));
	int j = (int)(q);
	q = floor(t.x[0] / _pi16.x[0] + 0.5);
	t = sub_dd_dd(t, mul_dd_d(_pi16, q));
	int k = (int)(q);
	int abs_k = abs(k);

	if (j < -2 || j > 2)
	{
		fprintf(stderr, "(dd_real::cos): Cannot reduce modulo pi/2.");
		return DD_NAN;
	}

	if (abs_k > 4)
	{
		fprintf(stderr, "(dd_real::cos): Cannot reduce modulo pi/16.");
		return DD_NAN;
	}

	if (k == 0)
	{
		switch (j)
		{
		case 0:
			return cos_taylor(t);
		case 1:
			return neg_dd(sin_taylor(t));
		case -1:
			return sin_taylor(t);
		default:
			return neg_dd(cos_taylor(t));
		}
	}

	dd_real sin_t, cos_t;
	sincos_taylor(t, &sin_t, &cos_t);
	dd_real u = dd_init2(cos_table[abs_k - 1][0], cos_table[abs_k - 1][1]);
	dd_real v = dd_init2(sin_table[abs_k - 1][0], sin_table[abs_k - 1][1]);

	if (j == 0)
	{
		if (k > 0)
		{
			r = sub_dd_dd(mul_dd_dd(u, cos_t), mul_dd_dd(v, sin_t));
		}
		else
		{
			r = add_dd_dd(mul_dd_dd(u, cos_t), mul_dd_dd(v, sin_t));
		}
	}
	else if (j == 1)
	{
		if (k > 0)
		{
			r = neg_dd(add_dd_dd(mul_dd_dd(u, sin_t), mul_dd_dd(v, sin_t)));
		}
		else
		{
			r = sub_dd_dd(mul_dd_dd(v, cos_t), mul_dd_dd(u, sin_t));
		}
	}
	else if (j == -1)
	{
		if (k > 0)
		{
			r = add_dd_dd(mul_dd_dd(u, sin_t), mul_dd_dd(v, cos_t));
		}
		else
		{
			r = sub_dd_dd(mul_dd_dd(u, sin_t), mul_dd_dd(v, cos_t));
		}
	}
	else
	{
		if (k > 0)
		{
			r = sub_dd_dd(mul_dd_dd(v, sin_t), mul_dd_dd(u, cos_t));
		}
		else
		{
			r = neg_dd(add_dd_dd(mul_dd_dd(u, cos_t), mul_dd_dd(v, sin_t)));
		}
	}

	return r;
}

void sincos_dd(const dd_real a, dd_real *sin_a, dd_real *cos_a)
{

	if (is_zero(a))
	{
		*sin_a = dd_init(0.0);
		*cos_a = dd_init(1.0);
		return;
	}

	// approximately reduce modulo 2*pi
	dd_real z = nint_dd(div_dd_dd(a, DD_2PI));
	dd_real r = sub_dd_dd(a, mul_dd_dd(DD_2PI, z));

	// approximately reduce module pi/2 and pi/16
	dd_real t;
	double q = floor(r.x[0] / DD_PI2.x[0] + 0.5);
	t = sub_dd_dd(r, mul_dd_d(DD_PI2, q));
	int j = (int)(q);
	int abs_j = abs(j);
	q = floor(t.x[0] / _pi16.x[0] + 0.5);
	t = sub_dd_dd(t, mul_dd_d(_pi16, q));
	int k = (int)(q);
	int abs_k = abs(k);

	if (abs_j > 2)
	{
		fprintf(stderr, "(dd_real::sincos): Cannot reduce modulo pi/2.");
		*cos_a = *sin_a = DD_NAN;
		return;
	}

	if (abs_k > 4)
	{
		fprintf(stderr, "(dd_real::sincos): Cannot reduce modulo pi/16.");
		*cos_a = *sin_a = DD_NAN;
		return;
	}

	dd_real sin_t, cos_t;
	dd_real s, c;

	sincos_taylor(t, &sin_t, &cos_t);

	if (abs_k == 0)
	{
		s = sin_t;
		c = cos_t;
	}
	else
	{
		dd_real u = dd_init2(cos_table[abs_k - 1][0], cos_table[abs_k - 1][1]);
		dd_real v = dd_init2(sin_table[abs_k - 1][0], sin_table[abs_k - 1][1]);

		if (k > 0)
		{
			s = add_dd_dd(mul_dd_dd(u, sin_t), mul_dd_dd(v, cos_t));
			c = sub_dd_dd(mul_dd_dd(u, cos_t), mul_dd_dd(v, sin_t));
		}
		else
		{
			s = sub_dd_dd(mul_dd_dd(u, sin_t), mul_dd_dd(v, cos_t));
			c = add_dd_dd(mul_dd_dd(u, cos_t), mul_dd_dd(v, sin_t));
		}
	}

	if (abs_j == 0)
	{
		*sin_a = s;
		*cos_a = c;
	}
	else if (j == 1)
	{
		*sin_a = c;
		*cos_a = neg_dd(s);
	}
	else if (j == -1)
	{
		*sin_a = neg_dd(c);
		*cos_a = s;
	}
	else
	{
		*sin_a = neg_dd(s);
		*cos_a = neg_dd(c);
	}
}

dd_real atan2_dd_dd(const dd_real y, const dd_real x)
{
	/* Strategy: Instead of using Taylor series to compute
	   arctan, we instead use Newton's iteration to solve
	   the equation

		  sin(z) = y/r    or    cos(z) = x/r

	   where r = sqrt(x^2 + y^2).
	   The iteration is given by

		  z' = z + (y - sin(z)) / cos(z)          (for equation 1)
		  z' = z - (x - cos(z)) / sin(z)          (for equation 2)

	   Here, x and y are normalized so that x^2 + y^2 = 1.
	   If |x| > |y|, then first iteration is used since the
	   denominator is larger.  Otherwise, the second is used.
	*/

	if (is_zero(x))
	{

		if (is_zero(y))
		{
			/* Both x and y is zero. */
			fprintf(stderr, "(dd_real::atan2): Both arguments zero.");
			return DD_NAN;
		}

		return (is_positive(y)) ? DD_PI2 : neg_dd(DD_PI2);
	}
	else if (is_zero(y))
	{
		return (is_positive(x)) ? dd_init(0.0) : DD_PI;
	}

	if (eq_dd_dd(x, y))
	{
		return (is_positive(y)) ? DD_PI4 : neg_dd(DD_3PI4);
	}

	if (eq_dd_dd(x, neg_dd(y)))
	{
		return (is_positive(y)) ? DD_3PI4 : neg_dd(DD_PI4);
	}

	dd_real r = sqrt_dd(add_dd_dd(sqr_dd(x), sqr_dd(y)));
	dd_real xx = div_dd_dd(x, r);
	dd_real yy = div_dd_dd(y, r);

	/* Compute double precision approximation to atan. */
	dd_real z = dd_init(atan2(y.x[0], x.x[0]));
	dd_real sin_z, cos_z;

	if (fabs(xx.x[0]) > fabs(yy.x[0]))
	{
		/* Use Newton iteration 1.  z' = z + (y - sin(z)) / cos(z)  */
		sincos_dd(z, &sin_z, &cos_z);
		z = add_dd_dd(z, div_dd_dd(sub_dd_dd(yy, sin_z), cos_z));
	}
	else
	{
		/* Use Newton iteration 2.  z' = z - (x - cos(z)) / sin(z)  */
		sincos_dd(z, &sin_z, &cos_z);
		z = sub_dd_dd(z, div_dd_dd(sub_dd_dd(xx, cos_z), sin_z));
	}

	return z;
}

dd_real tan_dd(const dd_real a)
{
	dd_real s, c;
	sincos_dd(a, &s, &c);
	return div_dd_dd(s, c);
}

dd_real asin_dd(const dd_real a)
{
	dd_real abs_a = abs_dd(a);

	if (gt_dd_d(abs_a, 1.0))
	{
		fprintf(stderr, "(dd_real::asin): Argument out of domain.");
		return DD_NAN;
	}

	if (is_one(abs_a))
	{
		return (is_positive(a)) ? DD_PI2 : neg_dd(DD_PI2);
	}

	return atan2_dd_dd(a, sqrt_dd(sub_d_dd(1.0, sqr_dd(a))));
}

dd_real acos_dd(const dd_real a)
{
	dd_real abs_a = abs_dd(a);

	if (gt_dd_d(abs_a, 1.0))
	{
		fprintf(stderr, "(dd_real::acos): Argument out of domain.");
		return dd_init2(NAN, NAN);
	}

	if (is_one(abs_a))
	{
		return (is_positive(a)) ? dd_init(0.0) : DD_PI;
	}

	return atan2_dd_dd(sqrt_dd(sub_d_dd(1.0, sqr_dd(a))), a);
}

dd_real atan_dd(const dd_real a)
{
	return atan2_dd_dd(a, dd_init(1.0));
}

dd_real sinh_dd(const dd_real a)
{
	if (is_zero(a))
	{
		return dd_init(0.0);
	}

	if (gt_dd_d(abs_dd(a), 0.05))
	{
		dd_real ea = exp_dd(a);
		return mul_pwr2_dd_d(sub_dd_dd(ea, inv_dd(ea)), 0.5);
	}

	/* since a is small, using the above formula gives
	   a lot of cancellation.  So use Taylor series.   */
	dd_real s = a;
	dd_real t = a;
	dd_real r = sqr_dd(t);
	double m = 1.0;
	double thresh = fabs(a.x[0] * DD_EPS);

	do
	{
		m += 2.0;
		t = mul_dd_dd(t, r);
		t = div_dd_d(t, ((m - 1) * m));

		s = add_dd_dd(s, t);
	} while (gt_dd_d(abs_dd(t), thresh));

	return s;
}

dd_real cosh_dd(const dd_real a)
{
	if (is_zero(a))
	{
		return dd_init(1.0);
	}

	dd_real ea = exp_dd(a);
	return mul_pwr2_dd_d(add_dd_dd(ea, inv_dd(ea)), 0.5);
}

dd_real tanh_dd(const dd_real a)
{
	if (is_zero(a))
	{
		return dd_init(0.0);
	}

	if (fabs(a.x[0]) > 0.05)
	{
		dd_real ea = exp_dd(a);
		dd_real inv_ea = inv_dd(ea);
		return div_dd_dd(sub_dd_dd(ea, inv_ea), add_dd_dd(ea, inv_ea));
	}
	else
	{
		dd_real s, c;
		s = sinh_dd(a);
		c = sqrt_dd(add_d_dd(1.0, sqr_dd(s)));
		return div_dd_dd(s, c);
	}
}

void sincosh_dd(const dd_real a, dd_real *s, dd_real *c)
{
	if (fabs(a.x[0]) <= 0.05)
	{
		*s = sinh_dd(a);
		*c = sqrt_dd(add_d_dd(1.0, sqr_dd(*s)));
	}
	else
	{
		dd_real ea = exp_dd(a);
		dd_real inv_ea = inv_dd(ea);
		*s = mul_pwr2_dd_d(sub_dd_dd(ea, inv_ea), 0.5);
		*c = mul_pwr2_dd_d(add_dd_dd(ea, inv_ea), 0.5);
	}
}

dd_real asinh_dd(const dd_real a)
{
	return log_dd(add_dd_dd(a, sqrt_dd(add_dd_d(sqr_dd(a), 1.0))));
}

dd_real acosh_dd(const dd_real a)
{
	if (lt_dd_d(a, 1.0))
	{
		fprintf(stderr, "(dd_real::acosh): Argument out of domain.");
		return DD_NAN;
	}

	return log_dd(add_dd_dd(a, sqrt_dd(sub_dd_d(sqr_dd(a), 1.0))));
}

dd_real atanh_dd(const dd_real a)
{
	if (ge_dd_d(abs_dd(a), 1.0))
	{
		fprintf(stderr, "(dd_real::atanh): Argument out of domain.");
		return DD_NAN;
	}

	return mul_pwr2_dd_d(log_dd(div_dd_dd(add_d_dd(1.0, a), sub_d_dd(1.0, a))), 0.5);
}
