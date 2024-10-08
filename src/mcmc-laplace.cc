/* ---------------------------------------------------------------------
 *
 * Copyright (C) 2019 by the deal.II authors and Wolfgang Bangerth.
 *
 * This file is part of the deal.II library.
 *
 * The deal.II library is free software; you can use it, redistribute
 * it, and/or modify it under the terms of the GNU Lesser General
 * Public License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * The full text of the license can be found in the file LICENSE.md at
 * the top level directory of deal.II.
 *
 * ---------------------------------------------------------------------

 *
 * Author: Wolfgang Bangerth, Colorado State University, 2019.
 */



#include <deal.II/grid/tria.h>
#include <deal.II/dofs/dof_handler.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/grid_refinement.h>
#include <deal.II/grid/tria_accessor.h>
#include <deal.II/grid/tria_iterator.h>
#include <deal.II/dofs/dof_accessor.h>
#include <deal.II/fe/fe_q.h>
#include <deal.II/dofs/dof_tools.h>
#include <deal.II/fe/fe_values.h>
#include <deal.II/base/quadrature_lib.h>
#include <deal.II/base/function.h>
#include <deal.II/base/parameter_handler.h>
#include <deal.II/base/multithread_info.h>
#include <deal.II/numerics/vector_tools.h>
#include <deal.II/numerics/matrix_tools.h>
#include <deal.II/numerics/fe_field_function.h>
#include <deal.II/lac/vector.h>
#include <deal.II/lac/full_matrix.h>
#include <deal.II/lac/sparse_matrix.h>
#include <deal.II/lac/dynamic_sparsity_pattern.h>
#include <deal.II/lac/solver_cg.h>
#include <deal.II/lac/precondition.h>
#include <deal.II/lac/sparse_ilu.h>

#include <deal.II/numerics/data_out.h>
#include <deal.II/numerics/solution_transfer.h>
#include <deal.II/numerics/error_estimator.h>

#include <fstream>
#include <iostream>
#include <random>
#include <thread>
#include <memory>

#if defined(SPEC)
double spec_exp10(double x) {return pow(10,x);}
#endif

#include <deal.II/base/logstream.h>

#include <sampleflow/producers/differential_evaluation_mh.h>
#include <sampleflow/filters/take_every_nth.h>
#include <sampleflow/filters/take_n_every_m.h>
#include <sampleflow/filters/component_splitter.h>
#include <sampleflow/filters/pass_through.h>
#include <sampleflow/filters/conversion.h>

#include <sampleflow/consumers/mean_value.h>
#include <sampleflow/consumers/count_samples.h>
#include <sampleflow/consumers/histogram.h>
#include <sampleflow/consumers/pair_histogram.h>
#include <sampleflow/consumers/maximum_probability_sample.h>
#include <sampleflow/consumers/covariance_matrix.h>
#include <sampleflow/consumers/stream_output.h>
#include <sampleflow/consumers/action.h>

#include <sampleflow/consumers/covariance_matrix.h>
#include <sampleflow/consumers/acceptance_ratio.h>
#include <sampleflow/consumers/average_cosinus.h>
#include <sampleflow/consumers/auto_covariance_matrix.h>
#include <sampleflow/consumers/auto_covariance_trace.h>


using SampleType = dealii::Vector<double>;

namespace Filters
{
  /**
   * An implementation of the Filter interface in which a given component
   * of a vector-valued sample is passed on. This useful if, for example,
   * one wants to compute the mean value or standard deviation of an
   * individual component of a sample vector is of interest.
   *
   *
   * ### Threading model ###
   *
   * The implementation of this class is thread-safe, i.e., its
   * filter() member function can be called concurrently and from multiple
   * threads.
   *
   *
   * @tparam InputType The C++ type used to describe the incoming samples.
   *   For the current class, the output type of samples is the `value_type`
   *   of the `InputType`, i.e., `typename InputType::value_type`, as this
   *   indicates the type of individual components of the `InputType`.
   */
  template <typename InputType>
  class ComponentPairSplitter : public SampleFlow::Filter<InputType, std::array<typename InputType::value_type,2>>
  {
    public:
    /**
     * Constructor.
     *
     * @param[in] selected_component The index of the component that is to
     *   be selected.
     */
    ComponentPairSplitter (const unsigned int selected_component_1,
                           const unsigned int selected_component_2);

    /**
     * Copy constructor.
     */
    ComponentPairSplitter (const ComponentPairSplitter<InputType> &o);

    /**
     * Destructor. This function also makes sure that all samples this
     * object may have received have been fully processed. To this end,
     * it calls the Consumers::disconnect_and_flush() function of the
     * base class.
     */
    virtual ~ComponentPairSplitter ();

    /**
     * Process one sample by extracting a given component and passing
     * that on as a sample in its own right to downstream consumers.
     *
     * @param[in] sample The sample to process.
     * @param[in] aux_data Auxiliary data about this sample. The current
     *   class does not know what to do with any such data and consequently
     *   simply passes it on.
     *
     * @return The selected component of the sample and the auxiliary data
     *   originally associated with the sample.
     */
    virtual
      boost::optional<std::pair<std::array<typename InputType::value_type,2>, SampleFlow::AuxiliaryData>>
      filter (InputType sample,
              SampleFlow::AuxiliaryData aux_data) override;

    private:
    /**
     * The selected component of samples to be extracted.
     */
    const std::array<unsigned int,2> selected_components;
  };



  template <typename InputType>
  ComponentPairSplitter<InputType>::
  ComponentPairSplitter (const unsigned int selected_component_1,
                         const unsigned int selected_component_2)
  : selected_components({{selected_component_1, selected_component_2}})
  {}



  template <typename InputType>
  ComponentPairSplitter<InputType>::
  ComponentPairSplitter (const ComponentPairSplitter<InputType> &o)
  : selected_components(o.selected_components)
  {}



  template <typename InputType>
  ComponentPairSplitter<InputType>::
  ~ComponentPairSplitter ()
  {
    this->disconnect_and_flush();
  }



  template <typename InputType>
  boost::optional<std::pair<std::array<typename InputType::value_type,2>, SampleFlow::AuxiliaryData> >
  ComponentPairSplitter<InputType>::
  filter (InputType sample,
          SampleFlow::AuxiliaryData aux_data)
  {
    assert (selected_components[0] < sample.size());
    assert (selected_components[1] < sample.size());

    return std::pair<std::array<typename InputType::value_type,2>, SampleFlow::AuxiliaryData>
      {
        {
          {
            std::move(sample[selected_components[0]]),
              std::move(sample[selected_components[1]])
              }
        },
        std::move(aux_data)
      };
  }


  // Downscale the 64-dimensional vector to one that has only 4
  // components. This improves numerical stability. We do the
  // downsampling by assigning each of the 64=8x8 parameters to one of
  // the four quadrants of the domain. This is the function that does
  // this transformation from the fine to the coarse sample.
  SampleType downscaler (const SampleType &vector_64)
  {
    constexpr unsigned int fine_to_coarse_map[4][16]
      =
      {{  0,1,2,3,
          8,9,10,11,
          16,17,18,19,
          24,25,26,27 },
       {  4,5,6,7,
          12,13,14,15,
          20,21,22,23,
          28,29,30,31 },
       {  32,33,34,35,
          40,41,42,43,
          48,49,50,51,
          56,57,58,59 },
       {  36,37,38,39,
          44,45,46,47,
          52,53,54,55,
          60,61,62,63 }};

    SampleType vector_4 (4);
    vector_4 = 0;
    for (unsigned int i=0; i<4; ++i)
      for (unsigned int j=0; j<16; ++j)
        vector_4[i] += vector_64[fine_to_coarse_map[i][j]];
    vector_4 /= 16;

    return vector_4;
  };
}



using namespace dealii;


// The following is a namespace in which we define the solver of the PDE.
// The main class implements an abstract `Interface` class declared at
// the top, which provides for an `evaluate()` function that, given
// a coefficient vector, solves the PDE discussed in the Readme file
// and then evaluates the solution at the 169 mentioned points.
//
// The solver follows the basic layout of step-4, though it precomputes
// a number of things in the `setup_system()` function, such as the
// evaluation of the matrix that corresponds to the point evaluations,
// as well as the local contributions to matrix and right hand side.
//
// Rather than commenting on everything in detail, in the following
// we will only document those things that are not already clear from
// step-4 and a small number of other tutorial programs.
namespace ForwardSimulator
{
  class Interface
  {
  public:
    virtual Vector<double> evaluate(const Vector<double> &coefficients) const = 0;

    virtual ~Interface() = default;
  };



  template <int dim>
  class PoissonSolver : public Interface
  {
  public:
    PoissonSolver(const unsigned int global_refinements,
                  const unsigned int fe_degree);
    virtual Vector<double>
    evaluate(const Vector<double> &coefficients) const override;

    std::string create_vtk_output (const Vector<double> &sample) const;
    std::string interpolate_to_finer_mesh(const Vector<double> &sample);

  private:
    void make_grid(const unsigned int global_refinements);
    void setup_system();
    void assemble_system(const Vector<double> &coefficients,
                         SparseMatrix<double> &system_matrix,
                         Vector<double>       &solution,
                         Vector<double>       &system_rhs) const;
    void solve(const SparseMatrix<double> &system_matrix,
               Vector<double>             &solution,
               const Vector<double>       &system_rhs) const;

    Triangulation<dim>        triangulation;
    FE_Q<dim>                 fe;
    DoFHandler<dim>           dof_handler;

    FullMatrix<double>        cell_matrix;
    Vector<double>            cell_rhs;
    std::map<types::global_dof_index,double> boundary_values;

    SparsityPattern           sparsity_pattern;

    std::vector<Point<dim>>   measurement_points;

    SparsityPattern           measurement_sparsity;
    SparseMatrix<double>      measurement_matrix;
  };



  template <int dim>
  PoissonSolver<dim>::PoissonSolver(const unsigned int global_refinements,
                                    const unsigned int fe_degree)
    : fe(fe_degree)
    , dof_handler(triangulation)
  {
    make_grid(global_refinements);
    setup_system();
  }



  template <int dim>
  void PoissonSolver<dim>::make_grid(const unsigned int global_refinements)
  {
    Assert(global_refinements >= 3,
           ExcMessage("This program makes the assumption that the mesh for the "
                      "solution of the PDE is at least as fine as the one used "
                      "in the definition of the coefficient."));
    GridGenerator::hyper_cube(triangulation, 0, 1);
    triangulation.refine_global(global_refinements);
  }



  template <int dim>
  void PoissonSolver<dim>::setup_system()
  {
    // First define the finite element space:
    dof_handler.distribute_dofs(fe);

    // Then set up the main data structures that will hold the discrete problem:
    {
      DynamicSparsityPattern dsp(dof_handler.n_dofs());
      DoFTools::make_sparsity_pattern(dof_handler, dsp);
      sparsity_pattern.copy_from(dsp);
    }

    // And then define the tools to do point evaluation. We choose
    // a set of 13x13 points evenly distributed across the domain:
    {
      const unsigned int n_points_per_direction = 13;
      const double       dx = 1. / (n_points_per_direction + 1);

      for (unsigned int x = 1; x <= n_points_per_direction; ++x)
        for (unsigned int y = 1; y <= n_points_per_direction; ++y)
          measurement_points.emplace_back(x * dx, y * dx);

      // First build a full matrix of the evaluation process. We do this
      // even though the matrix is really sparse -- but we don't know
      // which entries are nonzero. Later, the `copy_from()` function
      // calls build a sparsity pattern and a sparse matrix from
      // the dense matrix.
      Vector<double>     weights(dof_handler.n_dofs());
      FullMatrix<double> full_measurement_matrix(n_points_per_direction *
                                                   n_points_per_direction,
                                                 dof_handler.n_dofs());

      for (unsigned int index = 0; index < measurement_points.size(); ++index)
        {
          VectorTools::create_point_source_vector(dof_handler,
                                                  measurement_points[index],
                                                  weights);
          for (unsigned int i = 0; i < dof_handler.n_dofs(); ++i)
            full_measurement_matrix(index, i) = weights(i);
        }

      measurement_sparsity.copy_from(full_measurement_matrix);
      measurement_matrix.reinit(measurement_sparsity);
      measurement_matrix.copy_from(full_measurement_matrix);
    }

    // Next build the mapping from cell to the index in the 64-element
    // coefficient vector:
    for (const auto &cell : triangulation.active_cell_iterators())
      {
        const unsigned int i = std::floor(cell->center()[0] * 8);
        const unsigned int j = std::floor(cell->center()[1] * 8);

        const unsigned int index = i + 8 * j;

        cell->set_user_index(index);
      }

    // Finally prebuild the building blocks of the linear system as
    // discussed in the Readme file:
    {
      const unsigned int dofs_per_cell = fe.dofs_per_cell;

      cell_matrix.reinit(dofs_per_cell, dofs_per_cell);
      cell_rhs.reinit(dofs_per_cell);

      const QGauss<dim>  quadrature_formula(fe.degree+1);
      const unsigned int n_q_points = quadrature_formula.size();

      FEValues<dim> fe_values(fe,
                              quadrature_formula,
                              update_values | update_gradients |
                                update_JxW_values);

      fe_values.reinit(dof_handler.begin_active());

      for (unsigned int q_index = 0; q_index < n_q_points; ++q_index)
        for (unsigned int i = 0; i < dofs_per_cell; ++i)
          {
            for (unsigned int j = 0; j < dofs_per_cell; ++j)
              cell_matrix(i, j) +=
                (fe_values.shape_grad(i, q_index) * // grad phi_i(x_q)
                 fe_values.shape_grad(j, q_index) * // grad phi_j(x_q)
                 fe_values.JxW(q_index));           // dx

            cell_rhs(i) += (fe_values.shape_value(i, q_index) * // phi_i(x_q)
                            10.0 *                              // f(x_q)
                            fe_values.JxW(q_index));            // dx
          }

      VectorTools::interpolate_boundary_values(dof_handler,
                                               0,
                                               Functions::ZeroFunction<dim>(),
                                               boundary_values);
    }
  }



  // Given that we have pre-built the matrix and right hand side contributions
  // for a (representative) cell, the function that assembles the matrix is
  // pretty short and straightforward:
  template <int dim>
  void PoissonSolver<dim>::assemble_system(const Vector<double> &coefficients,
                                           SparseMatrix<double> &system_matrix,
                                           Vector<double>       &solution,
                                           Vector<double>       &system_rhs) const
  {
    Assert(coefficients.size() == 64, ExcInternalError());

    system_matrix = 0;
    system_rhs    = 0;

    const unsigned int dofs_per_cell = fe.dofs_per_cell;

    std::vector<types::global_dof_index> local_dof_indices(dofs_per_cell);

    for (const auto &cell : dof_handler.active_cell_iterators())
      {
        const double coefficient = coefficients(cell->user_index());

        cell->get_dof_indices(local_dof_indices);
        for (unsigned int i = 0; i < dofs_per_cell; ++i)
          {
            for (unsigned int j = 0; j < dofs_per_cell; ++j)
              system_matrix.add(local_dof_indices[i],
                                local_dof_indices[j],
                                coefficient * cell_matrix(i, j));

            system_rhs(local_dof_indices[i]) += cell_rhs(i);
          }
      }

    MatrixTools::apply_boundary_values(boundary_values,
                                       system_matrix,
                                       solution,
                                       system_rhs);
  }


  // The same is true for the function that solves the linear system:
  template <int dim>
  void PoissonSolver<dim>::solve(const SparseMatrix<double> &system_matrix,
                                 Vector<double>             &solution,
                                 const Vector<double>       &system_rhs) const
  {
    SparseILU<double> ilu;
    ilu.initialize(system_matrix);
    SolverControl control(100, 1e-6*system_rhs.l2_norm(), false, false);
    SolverCG<> solver(control);
    try
      {
        solver.solve(system_matrix, solution, system_rhs, ilu);
      }
    catch (const std::exception &)
      {
        try
          {
            PreconditionSSOR<> ssor;
            ssor.initialize(system_matrix);
            SolverControl control(solution.size(), 1e-6*system_rhs.l2_norm());
            SolverCG<> solver(control);
            solver.solve(system_matrix, solution, system_rhs, ssor);
          }
        catch (std::exception &exc)
        {
          std::cout << exc.what() << std::endl;
          std::abort();
        }
      }
  }



  // The following is the main function of this class: Given a coefficient
  // vector, it assembles the linear system, solves it, and then evaluates
  // the solution at the measurement points by applying the measurement
  // matrix to the solution vector. That vector of "measured" values
  // is then returned.
  //
  // The function will also output the solution in a graphical format
  // if you un-comment the corresponding statement in the third
  // code block. However, you may end up with a very large amount
  // of data: This code is producing, at the minimum, 10,000 samples
  // and creating output for each one of them is surely more data
  // than you ever want to see!
  //
  // At the end of the function, we output some timing information
  // every 10,000 samples.
  template <int dim>
  Vector<double>
  PoissonSolver<dim>::evaluate(const Vector<double> &coefficients) const
  {
    SparseMatrix<double>      system_matrix(sparsity_pattern);

    Vector<double>            solution(dof_handler.n_dofs());
    Vector<double>            system_rhs(dof_handler.n_dofs());

    assemble_system(coefficients,
                    system_matrix, solution, system_rhs);
    solve(system_matrix, solution, system_rhs);

    Vector<double> measurements(measurement_matrix.m());
    measurement_matrix.vmult(measurements, solution);
    Assert(measurements.size() == measurement_points.size(),
           ExcInternalError());

    return measurements;
  }



  template <int dim>
  std::string
  PoissonSolver<dim>::create_vtk_output(const Vector<double> &coefficients) const
  {
    SparseMatrix<double>      system_matrix(sparsity_pattern);

    Vector<double>            solution(dof_handler.n_dofs());
    Vector<double>            system_rhs(dof_handler.n_dofs());

    assemble_system(coefficients,
                    system_matrix, solution, system_rhs);
    solve(system_matrix, solution, system_rhs);


    std::ostringstream out;
    DataOut<dim> data_out;
    data_out.attach_dof_handler (dof_handler);
    data_out.add_data_vector (solution, "solution");
    data_out.build_patches();
    data_out.write_vtk (out);

    return out.str();
  }


  template <int dim>
  std::string
  PoissonSolver<dim>::interpolate_to_finer_mesh(const Vector<double> &coefficients)
  {
    Vector<double>            solution(dof_handler.n_dofs());
    {
      SparseMatrix<double>      system_matrix(sparsity_pattern);
      Vector<double>            system_rhs(dof_handler.n_dofs());

      assemble_system(coefficients,
                      system_matrix, solution, system_rhs);
      solve(system_matrix, solution, system_rhs);
    }
    
    // Create a 3d mesh, then use a cubic element on it:
    Triangulation<3> triangulation_3d;
    GridGenerator::hyper_cube(triangulation_3d, 0, 1);
    triangulation_3d.refine_global (3);

    FE_Q<3> fe_3d(3);
    DoFHandler<3> dof_handler_3d(triangulation_3d);
    dof_handler_3d.distribute_dofs(fe_3d);

    // Now interpolate the 2d solution onto the 3d mesh
    Vector<double> solution_3d (dof_handler_3d.n_dofs());
    Functions::FEFieldFunction<2> solution_2d_as_a_function(dof_handler, solution);
    ScalarFunctionFromFunctionObject<3> expand_2d_to_3d
      ([&] (const Point<3> &p)
       {
         return solution_2d_as_a_function.value(Point<2>(p[0], p[1]));
       }
      );
    VectorTools::interpolate (dof_handler_3d,
                              expand_2d_to_3d,
                              solution_3d);

    // Take the solution and interpolate it to a finer mesh several
    // times. Then create VTK output again on that fine mesh (which
    // usually we would write to disk, but here discard).
    //
    // The mesh refinement code is basically copied 1:1 from step-26
    for (unsigned int refinement_step=0; refinement_step<3; ++refinement_step)
      {
        Vector<float> estimated_error_per_cell(triangulation_3d.n_active_cells());

        KellyErrorEstimator<3>::estimate(
          dof_handler_3d,
          QGauss<3 - 1>(fe.degree + 1),
          std::map<types::boundary_id, const Function<3> *>(),
          solution_3d,
          estimated_error_per_cell);

        GridRefinement::refine_and_coarsen_fixed_fraction(triangulation_3d,
                                                          estimated_error_per_cell,
                                                          0.9,
                                                          0.1);
        SolutionTransfer<3> solution_trans(dof_handler_3d);

        Vector<double> previous_solution;
        previous_solution = solution_3d;
        triangulation_3d.prepare_coarsening_and_refinement();
        solution_trans.prepare_for_coarsening_and_refinement(previous_solution);

        triangulation_3d.execute_coarsening_and_refinement();
        for (const auto &cell : triangulation_3d.active_cell_iterators())
          {
            const unsigned int i = std::floor(cell->center()[0] * 8);
            const unsigned int j = std::floor(cell->center()[1] * 8);

            const unsigned int index = i + 8 * j;

            cell->set_user_index(index);
          }

        dof_handler_3d.distribute_dofs(fe_3d);

        solution_3d.reinit (dof_handler_3d.n_dofs());
        solution_trans.interpolate(previous_solution, solution_3d);

        AffineConstraints<double> constraints;
        DoFTools::make_hanging_node_constraints (dof_handler_3d, constraints);
        constraints.close();
        constraints.distribute (solution_3d);
      }

    // Put the solution on this fine mesh through DataOut again:
    std::ostringstream out;
    DataOut<3> data_out;
    data_out.attach_dof_handler (dof_handler_3d);
    data_out.add_data_vector (solution_3d, "solution");
    data_out.build_patches();
    data_out.write_vtk (out);

    return out.str();
  }
} // namespace ForwardSimulator


// The following namespaces define the statistical properties of the Bayesian
// inverse problem. The first is about the definition of the measurement
// statistics (the "likelihood"), which we here assume to be a normal
// distribution $N(\mu,\sigma I)$ with mean value $\mu$ given by the
// actual measurement vector (passed as an argument to the constructor
// of the `Gaussian` class and standard deviation $\sigma$.
//
// For reasons of numerical accuracy, it is useful to not return the
// actual likelihood, but its logarithm. This is because these
// values can be very small, occasionally on the order of $e^{-100}$,
// for which it becomes very difficult to compute accurate
// values.
namespace LogLikelihood
{
  class Interface
  {
  public:
    virtual double log_likelihood(const Vector<double> &x) const = 0;

    virtual ~Interface() = default;
  };


  class Gaussian : public Interface
  {
  public:
    Gaussian(const Vector<double> &mu, const double sigma);

    virtual double log_likelihood(const Vector<double> &x) const override;

  private:
    const Vector<double> mu;
    const double         sigma;
  };

  Gaussian::Gaussian(const Vector<double> &mu, const double sigma)
    : mu(mu)
    , sigma(sigma)
  {}


  double Gaussian::log_likelihood(const Vector<double> &x) const
  {
    Vector<double> x_minus_mu = x;
    x_minus_mu -= mu;

    return -x_minus_mu.norm_sqr() / (2 * sigma * sigma);
  }
} // namespace LogLikelihood


// Next up is the "prior" imposed on the coefficients. We assume
// that the logarithms of the entries of the coefficient vector
// are all distributed as a Gaussian with given mean and standard
// deviation. If the logarithms of the coefficients are normally
// distributed, then this implies in particular that the coefficients
// can only be positive, which is a useful property to ensure the
// well-posedness of the forward problem.
//
// For the same reasons as for the likelihood above, the interface
// for the prior asks for returning the *logarithm* of the prior,
// instead of the prior probability itself.
namespace LogPrior
{
  class Interface
  {
  public:
    virtual double log_prior(const Vector<double> &x) const = 0;

    virtual ~Interface() = default;
  };


  class LogGaussian : public Interface
  {
  public:
    LogGaussian(const double mu, const double sigma);

    virtual double log_prior(const Vector<double> &x) const override;

  private:
    const double mu;
    const double sigma;
  };

  LogGaussian::LogGaussian(const double mu, const double sigma)
    : mu(mu)
    , sigma(sigma)
  {}


  double LogGaussian::log_prior(const Vector<double> &x) const
  {
    double log_of_product = 0;

    for (const auto &el : x)
      log_of_product +=
        -(std::log(el) - mu) * (std::log(el) - mu) / (2 * sigma * sigma);

    return log_of_product;
  }
} // namespace LogPrior



// The Metropolis-Hastings algorithm requires a method to create a new sample
// given a previous sample. We do this by perturbing the current (coefficient)
// sample randomly using a Gaussian distribution centered at the current
// sample. To ensure that the samples' individual entries all remain
// positive, we use a Gaussian distribution in logarithm space -- in other
// words, instead of *adding* a small perturbation with mean value zero,
// we *multiply* the entries of the current sample by a factor that
// is the exponential of a random number with mean zero. (Because the
// exponential of zero is one, this means that the most likely factors
// to multiply the existing sample entries by are close to one. And
// because the exponential of a number is always positive, we never
// get negative samples this way.)
//
// But the Metropolis-Hastings sampler doesn't just need a perturbed
// sample $y$ location given the current sample location $x$. It also
// needs to know the ratio of the probability of reaching $y$ from
// $x$, divided by the probability of reaching $x$ from $y$. If we
// were to use a symmetric proposal distribution (e.g., a Gaussian
// distribution centered at $x$ with a width independent of $x$), then
// these two probabilities would be the same, and the ratio one. But
// that's not the case for the Gaussian in log space. It's not
// terribly difficult to verify that in that case, for a single
// component the ratio of these probabilities is $y_i/x_i$, and
// consequently for all components of the vector together, the
// probability is the product of these ratios.
namespace ProposalGenerator
{
  class Interface
  {
  public:
    virtual
    std::pair<Vector<double>,double>
    perturb(const Vector<double> &current_sample,
            std::mt19937 &random_number_generator) const = 0;

    virtual ~Interface() = default;
  };


  class LogGaussian : public Interface
  {
  public:
    LogGaussian(const double log_sigma);

    virtual
    std::pair<Vector<double>,double>
    perturb(const Vector<double> &current_sample,
            std::mt19937 &random_number_generator) const;

  private:
    const double         log_sigma;
  };



  LogGaussian::LogGaussian(const double       log_sigma)
    : log_sigma(log_sigma)
  {}


  std::pair<Vector<double>,double>
  LogGaussian::perturb(const Vector<double> &current_sample,
                       std::mt19937 &random_number_generator) const
  {
    Vector<double> new_sample = current_sample;
    double         product_of_ratios = 1;
    for (auto &x : new_sample)
      {
        const double rnd = SampleFlow::random::uniform_real_distribution<>(-log_sigma, log_sigma)(random_number_generator);
        const double exp_rnd = std::exp(rnd);
        x *= exp_rnd;
        product_of_ratios /= exp_rnd;
      }

    return {new_sample, product_of_ratios};
  }

} // namespace ProposalGenerator


namespace Postprocessing
{
  // Set up a post-processing step that simulates what we would really
  // do with samples if this wasn't a benchmark. We will connect it to
  // the a filter that only takes a subset of samples to make sure we
  // get (at least marginally) statistically independent samples, and
  // for each of these, we will then run the simulation on a
  // substantially finer grid, and create what would usually be
  // graphical output (except of course we don't write it to disk).
  //
  // In order to ensure that the output isn't just discarded (which
  // would create a dead code that a compiler could discard), we count
  // how many spaces the output contains. This requires actually
  // creating the output. We return this number of spaces, and
  // accumulate it over all samples that have been processed this way.
  std::pair<std::uint64_t,std::uint64_t>
  postprocess_to_finer_solution(const SampleType &sample)
  {
    // First set up a solver on a finer mesh and compute the forward
    // solution:
    ForwardSimulator::PoissonSolver<2> fine_solver(/* global_refinements = */ 5,
                                                   /* fe_degree = */ 2);
    const Vector<double> forward_solution = fine_solver.evaluate(sample);

    // Then put that forward solution into a string that represents
    // what we would write into a file. This being a benchmark, we
    // will discard the string, but not until we have recorded the size
    // of the string and counted the number of spaces:
    const std::string vtk_output = fine_solver.create_vtk_output (sample);

    // Finally, interpolate the solution so computed to an even finer
    // mesh and compute some statistics on that:
    const std::string fine_vtk_output = fine_solver.interpolate_to_finer_mesh (sample);

    return {vtk_output.size() + fine_vtk_output.size(),
      std::count (vtk_output.begin(), vtk_output.end(), ' ') +
      std::count (fine_vtk_output.begin(), fine_vtk_output.end(), ' ')};
  };
}





// The final function is `main()`, which simply puts all of these pieces
// together into one. The "exact solution", i.e., the "measurement values"
// we use for this program are tabulated to make it easier for other
// people to use in their own implementations of this benchmark. These
// values created using the same main class above, but using 8 mesh
// refinements and using a Q3 element -- i.e., using a much more accurate
// method than the one we use in the forward simulator for generating
// samples below (which uses 5 global mesh refinement steps and a Q1
// element). If you wanted to regenerate this set of numbers, then
// the following code snippet would do that:
// @code
//  /* Set the exact coefficient: */
//  Vector<double> exact_coefficients(64);
//  for (auto &el : exact_coefficients)
//    el = 1.;
//  exact_coefficients(9) = exact_coefficients(10) = exact_coefficients(17) =
//    exact_coefficients(18)                       = 0.1;
//  exact_coefficients(45) = exact_coefficients(46) = exact_coefficients(53) =
//    exact_coefficients(54)                        = 10.;
//
//  /* Compute the "correct" solution vector: */
//  const Vector<double> exact_solution =
//    ForwardSimulator::PoissonSolver<2>(/* global_refinements = */ 8,
//                                       /* fe_degree = */ 3)
//      .evaluate(exact_coefficients);
// @endcode
int main(int argc, char **argv)
{
  if ((argc < 2) || (argc > 2))
    {
      std::cout << "Call this program via the following command line:\n"
                << "     ./sample-flow <input.prm>\n"
                << "where <input.prm> is the name of an input file."
                << std::endl;
      std::exit(1);
    }

  unsigned int n_chains = 3;
  unsigned int n_samples_per_chain = 10000;

  ParameterHandler prm;
  prm.add_parameter ("Number of samples per chain", n_samples_per_chain, "",
                     Patterns::Integer ());
  prm.add_parameter ("Number of chains", n_chains, "",
                     Patterns::Integer (3,100));
  prm.parse_input (argv[1]);

  std::cout << "Running with " << n_chains << " chains, computing "
            << n_samples_per_chain << " samples per chain."
            << std::endl;



  // This benchmark does not use deal.II's TBB-based threading
  // capabilities to parallelize deal.II-internal functionality. (It
  // does, however, use threads at a higher level.) So it probably
  // doesn't matter whether or not we set a thread limit for these
  // internal operations, but it also doesn't do any harm.
  MultithreadInfo::set_thread_limit(1);

  const unsigned int random_seed  = 1U;

  const Vector<double> exact_solution(
    {   0.06076511762259369, 0.09601910120848481,
        0.1238852517838584,  0.1495184117375201,
        0.1841596127549784,  0.2174525028261122,
        0.2250996160898698,  0.2197954769002993,
        0.2074695698370926,  0.1889996477663016,
        0.1632722532153726,  0.1276782480038186,
        0.07711845915789312, 0.09601910120848552,
        0.2000589533367983,  0.3385592591951766,
        0.3934300024647806,  0.4040223892461541,
        0.4122329537843092,  0.4100480091545554,
        0.3949151637189968,  0.3697873264791232,
        0.33401826235924,    0.2850397806663382,
        0.2184260032478671,  0.1271121156350957,
        0.1238852517838611,  0.3385592591951819,
        0.7119285162766475,  0.8175712861756428,
        0.6836254116578105,  0.5779452419831157,
        0.5555615956136897,  0.5285181561736719,
        0.491439702849224,   0.4409367494853282,
        0.3730060082060772,  0.2821694983395214,
        0.1610176733857739,  0.1495184117375257,
        0.3934300024647929,  0.8175712861756562,
        0.9439154625527653,  0.8015904115095128,
        0.6859683749254024,  0.6561235366960599,
        0.6213197201867315,  0.5753611315000049,
        0.5140091754526823,  0.4325325506354165,
        0.3248315148915482,  0.1834600412730086,
        0.1841596127549917,  0.4040223892461832,
        0.6836254116578439,  0.8015904115095396,
        0.7870119561144977,  0.7373108331395808,
        0.7116558878070463,  0.6745179049094283,
        0.6235300574156917,  0.5559332704045935,
        0.4670304994474178,  0.3499809143811,
        0.19688263746294,    0.2174525028261253,
        0.4122329537843404,  0.5779452419831566,
        0.6859683749254372,  0.7373108331396063,
        0.7458811983178246,  0.7278968022406559,
        0.6904793535357751,  0.6369176452710288,
        0.5677443693743215,  0.4784738764865867,
        0.3602190632823262,  0.2031792054737325,
        0.2250996160898818,  0.4100480091545787,
        0.5555615956137137,  0.6561235366960938,
        0.7116558878070715,  0.727896802240657,
        0.7121928678670187,  0.6712187391428729,
        0.6139157775591492,  0.5478251665295381,
        0.4677122687599031,  0.3587654911000848,
        0.2050734291675918,  0.2197954769003094,
        0.3949151637190157,  0.5285181561736911,
        0.6213197201867471,  0.6745179049094407,
        0.690479353535786,   0.6712187391428787,
        0.6178408289359514,  0.5453605027237883,
        0.489575966490909,   0.4341716881061278,
        0.3534389974779456,  0.2083227496961347,
        0.207469569837099,   0.3697873264791366,
        0.4914397028492412,  0.5753611315000203,
        0.6235300574157017,  0.6369176452710497,
        0.6139157775591579,  0.5453605027237935,
        0.4336604929612851,  0.4109641743019312,
        0.3881864790111245,  0.3642640090182592,
        0.2179599909280145,  0.1889996477663011,
        0.3340182623592461,  0.4409367494853381,
        0.5140091754526943,  0.5559332704045969,
        0.5677443693743304,  0.5478251665295453,
        0.4895759664908982,  0.4109641743019171,
        0.395727260284338,   0.3778949322004734,
        0.3596268271857124,  0.2191250268948948,
        0.1632722532153683,  0.2850397806663325,
        0.373006008206081,   0.4325325506354207,
        0.4670304994474315,  0.4784738764866023,
        0.4677122687599041,  0.4341716881061055,
        0.388186479011099,   0.3778949322004602,
        0.3633362567187364,  0.3464457261905399,
        0.2096362321365655,  0.1276782480038148,
        0.2184260032478634,  0.2821694983395252,
        0.3248315148915535,  0.3499809143811097,
        0.3602190632823333,  0.3587654911000799,
        0.3534389974779268,  0.3642640090182283,
        0.35962682718569,    0.3464457261905295,
        0.3260728953424643,  0.180670595355394,
        0.07711845915789244, 0.1271121156350963,
        0.1610176733857757,  0.1834600412730144,
        0.1968826374629443,  0.2031792054737354,
        0.2050734291675885,  0.2083227496961245,
        0.2179599909279998,  0.2191250268948822,
        0.2096362321365551,  0.1806705953553887,
        0.1067965550010013                         });

  // Now run the forward simulator for samples:
  ForwardSimulator::PoissonSolver<2> laplace_problem(
    /* global_refinements = */ 5,
    /* fe_degree = */ 1);
  LogLikelihood::Gaussian        log_likelihood(exact_solution, 0.05);
  LogPrior::LogGaussian          log_prior(0, 2);
  ProposalGenerator::LogGaussian proposal_generator(0.09); /* so that the acceptance ratio is ~0.24 */

  Vector<double> starting_coefficients(64);
  for (auto &el : starting_coefficients)
    el = 1.;

  // Next declare the sampler and all of the filters and consumers we
  // need to create to evaluate the solution. Because the original
  // version of the code had more than one sampler, it is cumbersome
  // to connect all of these objects to all of the samplers; rather,
  // create a pass through filter that is itself connected to all
  // samplers, and that serves as inputs for all of the downstream
  // objects that then only have to be connected to a single producer
  // (namely, the pass through filter). This is no longer the case for
  // this version of the code, but we'll keep the structure.
  SampleFlow::Producers::DifferentialEvaluationMetropolisHastings<SampleType> sampler;

  SampleFlow::Filters::PassThrough<SampleType> pass_through;
  pass_through.connect_to_producer (sampler);

  // Consumer for counting how many samples we have processed
  SampleFlow::Consumers::CountSamples<SampleType> sample_count;
  sample_count.connect_to_producer (pass_through);

  // Consumer for computing the mean value
  SampleFlow::Consumers::MeanValue<SampleType> mean_value;
  mean_value.connect_to_producer (pass_through);

  // Consumer for computing the covariance matrix
  SampleFlow::Consumers::CovarianceMatrix<SampleType> cov_matrix;
  cov_matrix.connect_to_producer (pass_through);

  // Consumer for computing the MAP point
  SampleFlow::Consumers::MaximumProbabilitySample<SampleType> MAP_point;
  MAP_point.connect_to_producer (pass_through);

  // Consumers for histograms for each component. For this to work, we
  // first have to split each sample into its 64 individual
  // components, and then create histogram objects for each component
  // individually
  std::vector<SampleFlow::Filters::ComponentSplitter<SampleType>> component_splitters;
  std::vector<SampleFlow::Consumers::Histogram<SampleType::value_type>> histograms;
  component_splitters.reserve(64);
  histograms.reserve(64);
  for (unsigned int c=0; c<64; ++c)
    {
      component_splitters.emplace_back (c);
      component_splitters.back().connect_to_producer (pass_through);

#if defined(SPEC)
      histograms.emplace_back(-3,3,1000,&spec_exp10);
#else    
      histograms.emplace_back(-3,3,1000,&exp10);
#endif      
      histograms.back().connect_to_producer (component_splitters[c]);
    }

  // Consumer for computing the autocovariance (correlation). This is
  // a very expensive operation, and so we only consider every 100th
  // sample, and compute up to a lag of 200, which equates to a sample
  // of lag of 20,000
  SampleFlow::Filters::TakeEveryNth<SampleType> every_100th(100);
  every_100th.connect_to_producer (pass_through);

  SampleFlow::Consumers::AutoCovarianceMatrix<SampleType> autocovariance(200);
  autocovariance.connect_to_producer (every_100th);

  SampleFlow::Consumers::AutoCovarianceTrace<SampleType> autocovariance_trace(200);
  autocovariance_trace.connect_to_producer (every_100th);


  // Set up filters that separate out two pairs of components
  Filters::ComponentPairSplitter<SampleType> pair_splitter_45_46(45,46);
  pair_splitter_45_46.connect_to_producer (pass_through);

  Filters::ComponentPairSplitter<SampleType> pair_splitter_53_54(53,54);
  pair_splitter_53_54.connect_to_producer (pass_through);

  // Then also create the consumers that turn these pairs of
  // components into pair histograms
  SampleFlow::Consumers::PairHistogram<std::array<double,2>> pair_histogram_45_46 (0, 100, 300,
                                                                                   0, 100, 300);
  pair_histogram_45_46.connect_to_producer (pair_splitter_45_46);

  SampleFlow::Consumers::PairHistogram<std::array<double,2>> pair_histogram_53_54 (0, 100, 300,
                                                                                   0, 100, 300);
  pair_histogram_53_54.connect_to_producer (pair_splitter_53_54);

  std::ostringstream running_mean_error_output;
  auto compute_running_mean_error
    = [&](SampleType, SampleFlow::AuxiliaryData)
    {
          static const std::valarray<double> known_mean_value =
          {
                76.3181 ,
                1.2104 ,
                0.977381 ,
                0.882007 ,
                0.971859 ,
                0.947832 ,
                1.08529 ,
                11.3864 ,
                1.21193 ,
                0.0937216 ,
                0.115799 ,
                0.581515 ,
                0.947178 ,
                6.25794 ,
                9.33417 ,
                1.08151 ,
                0.977449 ,
                0.115796 ,
                0.460531 ,
                267.009 ,
                30.8675 ,
                7.18853 ,
                12.3898 ,
                0.949863 ,
                0.881977 ,
                0.582842 ,
                267.721 ,
                369.349 ,
                234.587 ,
                13.2892 ,
                22.3639 ,
                0.988806 ,
                0.9719 ,
                0.950947 ,
                30.7566 ,
                233.935 ,
                1.16897 ,
                0.832747 ,
                88.5244 ,
                0.987809 ,
                0.947816 ,
                6.25955 ,
                7.11919 ,
                13.1987 ,
                0.832702 ,
                176.728 ,
                283.378 ,
                0.914212 ,
                1.08521 ,
                9.38632 ,
                12.435 ,
                22.496 ,
                88.5744 ,
                283.41 ,
                218.647 ,
                0.933451 ,
                11.3544 ,
                1.08144 ,
                0.949869 ,
                0.98877 ,
                0.987866 ,
                0.914247 ,
                0.933426 ,
                1.59984
          };

          const SampleType current_mean = mean_value.get();
          std::valarray<double> mean(current_mean.begin(),
                                     current_mean.size());

          const std::valarray<double> diff = (mean - known_mean_value) / known_mean_value;
          double norm_sqr = 0;
          for (const auto p : diff)
            norm_sqr += p*p;

          running_mean_error_output << norm_sqr << '\n';

          static unsigned int counter = 0;
          if (counter % 50 == 0)
            running_mean_error_output << std::flush;
          ++counter;
    };
  SampleFlow::Filters::TakeEveryNth<SampleType> every_1000th(1000);
  every_1000th.connect_to_producer (pass_through);

  SampleFlow::Consumers::Action<SampleType> running_mean_error (compute_running_mean_error, true);
  running_mean_error.connect_to_producer (every_1000th);

  // Set up a post-processing step that simulates what we really do
  // with samples. We will connect it to a sub-sampling filter to
  // make sure we get (at least marginally) statistically independent
  // samples, and for each of these, we will then run the simulation
  // on a substantially finer grid, and create what would usually be
  // graphical output (except of course we don't write it to disk).
  //
  // We do this for all samples of a generation every 64 generations.
  SampleFlow::Filters::TakeNEveryM<SampleType> postprocess_subsampler(n_chains*64, n_chains);
  postprocess_subsampler.connect_to_producer (pass_through);
  SampleFlow::Filters::Conversion<SampleType,std::pair<std::uint64_t,std::uint64_t>>
    postprocess_finer_solution (&Postprocessing::postprocess_to_finer_solution);
  postprocess_finer_solution.connect_to_producer (postprocess_subsampler);

  std::atomic<std::uint64_t> total_output_size (0);
  std::atomic<std::uint64_t> total_number_of_spaces (0);
  SampleFlow::Consumers::Action<std::pair<std::uint64_t,std::uint64_t>>
    o ([&total_output_size,&total_number_of_spaces](const std::pair<std::uint64_t,std::uint64_t> s,
                                                    const SampleFlow::AuxiliaryData &)
    {
      total_output_size += s.first;
      total_number_of_spaces += s.second;
    });
  o.connect_to_producer (postprocess_finer_solution);

  auto print_periodic_output
    = [&](SampleType, SampleFlow::AuxiliaryData)
    {
          static unsigned int nth_sample = 100;
          std::cout << "Sample number " << nth_sample << std::endl;
          nth_sample += 100;
    };
  SampleFlow::Consumers::Action<SampleType> periodic_output (print_periodic_output, true);
  periodic_output.connect_to_producer (every_100th);


  // Downscale the 64-dimensional vector to one that has only 4
  // components. This improves numerical stability. We do the
  // downsampling by assigning each of the 64=8x8 parameters to one of
  // the four quadrants of the domain.
  SampleFlow::Filters::Conversion<SampleType,SampleType> downscaling (&Filters::downscaler);
  downscaling.connect_to_producer (sampler);

  SampleFlow::Consumers::MeanValue<SampleType> mean_value_4;
  mean_value_4.connect_to_producer (downscaling);

  // Finally, create the samples:
  std::mt19937 random_number_generator(random_seed);
  sampler.sample(std::vector<SampleType>(n_chains, starting_coefficients),
                 /* log_likelihood = */
                 [&](const SampleType &x) {
                   for (const auto &v : x)
                     if (v<=0)
                       return -std::numeric_limits<double>::infinity();
                   const double posterior
                     = (log_likelihood.log_likelihood(laplace_problem.evaluate(x)) +
                        log_prior.log_prior(x));
                   return posterior;
                 },
                 /* perturb = */
                 [&](const SampleType &x) {
                   return proposal_generator.perturb(x, random_number_generator);
                 },
                 /* crossover = */
                 [](const SampleType &current_sample,
                    const SampleType &sample_a,
                    const SampleType &sample_b)
                 -> std::pair<SampleType,double>
                 {
                   const double gamma = 2.38 / std::sqrt(2*current_sample.size());

                   // Compute 'current_sample + gamma * (sample_a - sample_b)'
                   SampleType result(sample_a);
                   result -= sample_b;
                   result *= gamma;
                   result += current_sample;
                   return {result,1.};
                 },
                 /* crossover_gap = */ n_samples_per_chain,
                 /* n_samples = */ n_samples_per_chain * n_chains,
                 /* asynchronous_likelihood_execution = */ true,
                 random_seed);

  // Then output some statistics
  std::cout << "Mean value of the 4-parameter downscaling:\n    ";
  for (const auto v : mean_value_4.get())
    std::cout << v << ' ';
  std::cout << std::endl;

  std::cout << "Comparison mean value of the downscaled 64-parameter mean:\n    ";
  for (const auto v : Filters::downscaler(mean_value.get()))
    std::cout << v << ' ';
  std::cout << std::endl;

  std::cout << "Total size of output over all upscaled samples:             "
            << total_output_size << std::endl;
  std::cout << "Total number of spaces in output over all upscaled samples: "
            << total_number_of_spaces << std::endl;

  std::cout << "Number of samples = " << sample_count.get() << std::endl;
}
