// Wrapper for FFTW version 3.

/*
  Support for key FFTW version 3 routines in Chapel trappings

  This module defines a Chapel wrapper around key routines from FFTW
  version 3.  Over time, the intention is to expand this support to
  additional routines, prioritizing based on user feedback.

  As in general FFTW, the flow is to:

  0) Call init_threads() and plan_with_nthreads() if using the
     multithreaded FFTW interface.

  1) Create plan(s) using the plan_dft*() routines.

  2) Execute the plan(s) using the execute() routine.

  3) Destroy the plan(s) using the destroy_plan() routine.

  4) If using the multithreaded FFTW interface, call
     cleanup_threads().  

  5) Call cleanup().

  At present, clients of the FFTW module must specify fftw3.h and -I,
  -L, -l flags sufficient to specify the required libraries and find
  the files on the 'chpl' command line.  We are currently
  investigating techniques to streamline this process.
*/

// TODOs for Brad (in priority order):
//
// - How can we get away from fftw_complex and use Chapel's complex?
//
// - How do we feel about FFTW_ALLCAPS names given that the routines
//   themselves don't have fftw_ prefixes, that they're defined as
//   c_int's, and that they're all-caps?
//
// - Related question about fftw_ types taking type c_int, though
//   maybe we don't care that much since symbolic values are taken
//   in?
//

module FFTW {

  use SysCTypes;

  // Define the various planner flags
  // See Sec. 4.3.2 of FFTW manual "Planner Flags"

  //
  // TODO: Is it weird that we use FFTW_ names here, but not in
  // the routines?  Should we wrap the external flags so that
  // they can be ints or enums and avoid the all-caps C scheme
  // or is the familiarity better?
  //

  extern const FFTW_FORWARD : c_int;
  /* These two constants are symbolic aliases for making the sign of
     the exponent in the formula that defines the Fourier transform
     -1 or 1, respectively. */
  extern const FFTW_BACKWARD : c_int;

  extern const FFTW_ESTIMATE : c_uint;
  extern const FFTW_MEASURE : c_uint;
  extern const FFTW_PATIENT : c_uint;
  extern const FFTW_EXHAUSTIVE : c_uint;
  /* 
     All of the above are planning-rigor flags.  See `Section 4.3.2
     <http://www.fftw.org/doc/Planner-Flags.html>`_ of the FFTW manual
     for details.
  */
  extern const FFTW_WISDOM_ONLY : c_uint;
  extern const FFTW_DESTROY_INPUT : c_uint;
  extern const FFTW_PRESERVE_INPUT : c_uint;
  /* 
     All of the above are algorithm-restriction flags.  See `Section
     4.3.2 <http://www.fftw.org/doc/Planner-Flags.html>`_ of the FFTW
     manual for details.
  */
  extern const FFTW_UNALIGNED : c_uint;


  // Define types
  // NOTE : Ideally, this should be mapped to complex(128), but I seem
  // to run into casting issues in the C code. This is a simple
  // workaround for now - although ugly.  Also note that if one used
  // complex types, then one would need to include complex.h at the
  // command line.
  //
  // TODO: What would it take to change these to complex rather than
  // fftw_complex?
  //
  /*
    At present, to send complex arrays to FFTW routines, they must be
    declared to be of type fftw_complex.  We anticipate adding support
    for using native Chapel complex types over time.
  */
  extern type fftw_complex = 2*real(64); // 4.1.1

  /*
    'fftw_plan' is an opaque type used by FFTW for storing and passing
    plans between routines.
  */
  extern type fftw_plan; // opaque type


  // Multi-threaded setup routines below
  /*
    Initialize the threads used for multi-threaded FFTW plans.

    :returns: Zero if there was an error, non-zero otherwise.
  */
  proc init_threads() : c_int {
    return C_FFTW.fftw_init_threads();
  }

  /*
    Registers the number of threads to use for multi-threaded FFTW
    plans.

    :arg nthreads: The number of threads to use.
    :type nthreads: int
   */
  proc plan_with_nthreads(nthreads: int) {
    C_FFTW.fftw_plan_with_nthreads(safe_cast(c_int, nthreads));
  }


  // Planner functions
  // Complex : 4.3.1
  // NOTE : We pass in arrays using ref 

  // TODO: Can we have the plan_dft() routine below take in native
  // Chapel types without changing the external C constants from
  // type c_int?  Probably given that Chapel's int will be >=
  // C's int for the forseeable future.  See also the TODO above
  // about whether we'd want to represent those constants using
  // more native Chapel types anyway.

  /*
    Creates a plan for a complex->complex DFT.

    :arg input: The input array, which can be of any rank
    :type input: [] :type:`fftw_complex`

    :arg output: The output array, with rank matching the input array's
    :type output: [] :type:`fftw_complex`
    
    :arg sign: The sign of the exponent in the DFT formula.
    :type sign: c_int

    :arg flags: planning-rigor and/or algorithm-restriction flags
    :type sign: c_int

    :returns: The fftw_plan representing the plan
  */
  proc plan_dft(input: [] fftw_complex, output: [] fftw_complex, 
                 sign: c_int, flags: c_uint) : fftw_plan
  {
    param rank = input.rank;

    var dims: rank*c_int;
    for param i in 1..rank do
      dims(i) = safe_cast(c_int, input.domain.dim(i).size);

    return C_FFTW.fftw_plan_dft(safe_cast(c_int, rank), dims, input, 
                                     output, sign, flags);
  }

  // Real-to-complex and complex-to-real planning routines
  // There are two cases that we treat independently here : in-place and out-of-place transforms

  // Since the calls to FFTW are the same, pull the extern declarations out.
  // TODO : This should be cleaned up further and made consistent across the file

  // Out-of-place routines
  proc plan_dft_r2c(in1 : [] real(64), out1 : [] fftw_complex, flags :c_uint) : fftw_plan
  {
    param rank = in1.rank: c_int;

    var dims: rank*c_int;
    for param i in 1..rank do
      dims(i) = in1.domain.dim(i).size: c_int;

    return C_FFTW.fftw_plan_dft_r2c(rank, dims, in1, out1, flags);
  }


  proc plan_dft_c2r(in1 : [] fftw_complex, out1 : [] real(64),  flags :c_uint) : fftw_plan
  {
    param rank = out1.rank: c_int; // The dimensions are that of the real array

    var dims: rank*c_int;
    for param i in 1..rank do
      dims(i) = out1.domain.dim(i).size: c_int;

    return C_FFTW.fftw_plan_dft_c2r(rank, dims, in1, out1, flags);
  }

  // In-place routines, note that these take in the true leading dimension
  // TODO : We should put in checks to see that the sizes are consistent
  // TODO : We should check on types...
  proc plan_dft_r2c(realDom : domain, in1 : [] ?t, flags : c_uint) : fftw_plan 
  {
    param rank = realDom.rank: c_int;

    var dims: rank*c_int;
    for param i in 1..rank do
      dims(i) = realDom.dim(i).size: c_int;

    return C_FFTW.fftw_plan_dft_r2c(rank, dims, in1, in1, flags);
  }
  proc plan_dft_c2r(realDom : domain, in1: [] ?t, flags : c_uint) : fftw_plan 
  {
    param rank = realDom.rank: c_int;

    var dims: rank*c_int;
    for param i in 1..rank do
      dims(i) = realDom.dim(i).size: c_int;

    return C_FFTW.fftw_plan_dft_c2r(rank, dims, in1, in1, flags);
  }


  // Using plans 
  /*
    Executes the given plan.

    :arg plan: The plan to execute, as computed by a plan*() routine.
    :type plan: fftw_plan
  */
  proc execute(const plan: fftw_plan) {
   C_FFTW.fftw_execute(plan);
  }

  /*
    Destroys the given plan.

    :arg plan: The plan to destroy
    :type plan: fftw_plan
  */
  proc destroy_plan(plan: fftw_plan) {
    C_FFTW.fftw_destroy_plan(plan);
  }


  /*
    Cleans up the threads if multi-threaded FFTW's were enabled using
    :proc:`init_threads()`.
  */
  proc cleanup_threads() {
    C_FFTW.fftw_cleanup_threads();
  }

  /*
    Cleans up FFTW overall.
  */
  proc cleanup() {
    C_FFTW.fftw_cleanup();
  }


  // Utilities -- not in FFTW
  /*
    Computes an absolute value on an :type:`fftw_complex` value.  This will
    be deprecated once we can replace :type:`fftw_complex` with Chapel's
    native complex type.
    
    :arg val: the input value
    :type val: :type:`fftw_complex`
  */
  proc abs(val: fftw_complex) : real(64) {
    const (r,c) = val;
    return sqrt(r*r + c*c);
  }

  /*
    Returns the real component of an :type:`fftw_complex` value.  This will be
    deprecated once we can replace :type:`fftw_complex` with Chapel's native
    complex type.
    
    :arg val: the input value
    :type val: :type:`fftw_complex`

    :returns: the real component of value as a `real(64)`
  */
  proc re(val: fftw_complex) : real(64) {
    return val(1);
  }

  //
  // TODO: Is there a world in which returning this as imaginary would
  // be preferable?
  //

  /*
    Returns the imaginary component of an :type:`fftw_complex` value.  This
    will be deprecated once we can replace :type:`fftw_complex` with Chapel's
    native complex type.
    
    :arg val: the input value
    :type val: :type:`fftw_complex`

    :returns: the imaginary component of value as a `real(64)`
  */
  proc im(val: fftw_complex) : real(64) {
    return val(2);
  }

  pragma "no doc"
  module C_FFTW {
    // CHPLDOC FIXME: I don't believe the following routines should
    // need to get labeled as "no doc" routines...
  pragma "no doc"
    extern proc fftw_plan_dft(rank: c_int, 
        n,  // BLC: having trouble being specific
        in1: [] fftw_complex, 
        out1: [] fftw_complex, 
        sign : c_int, c_flags : c_uint) : fftw_plan;

  pragma "no doc"
  extern proc fftw_plan_dft_r2c(rank: c_int, 
      n,  // BLC: having trouble being specific
      in1: [],
      out1: [], 
      c_flags : c_uint) : fftw_plan;

  pragma "no doc"
  extern proc fftw_plan_dft_c2r(rank: c_int, 
      n,  // BLC: having trouble being specific
      in1: [],
      out1: [],
      c_flags : c_uint) : fftw_plan;

  pragma "no doc"
    extern proc fftw_execute(const plan : fftw_plan);

  pragma "no doc"
    extern proc fftw_destroy_plan(plan : fftw_plan);

  pragma "no doc"
    extern proc fftw_cleanup();

  pragma "no doc"
    extern proc fftw_init_threads() : c_int;

  pragma "no doc"
    extern proc fftw_cleanup_threads();

  pragma "no doc"
    extern proc fftw_plan_with_nthreads(n : c_int);
  }
}