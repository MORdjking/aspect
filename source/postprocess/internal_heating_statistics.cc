/*
  Copyright (C) 2011 - 2014 by the authors of the ASPECT code.

  This file is part of ASPECT.

  ASPECT is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.

  ASPECT is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with ASPECT; see the file doc/COPYING.  If not see
  <http://www.gnu.org/licenses/>.
*/



#include <aspect/postprocess/internal_heating_statistics.h>
#include <aspect/simulator_access.h>

#include <deal.II/base/quadrature_lib.h>
#include <deal.II/fe/fe_values.h>

#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>


namespace aspect
{
  namespace Postprocess
  {
    template <int dim>
    std::pair<std::string,std::string>
    InternalHeatingStatistics<dim>::execute (TableHandler &statistics)
    {
      const HeatingModel::Interface<dim> &heating_model=this->get_heating_model();

      // create a quadrature formula based on the temperature element alone.
      const QGauss<dim> quadrature_formula (this->get_fe().base_element(this->introspection().base_elements.temperature).degree+1);
      const unsigned int n_q_points = quadrature_formula.size();

      FEValues<dim> fe_values (this->get_mapping(),
                               this->get_fe(),
                               quadrature_formula,
                               update_values   |
                               update_quadrature_points |
                               update_JxW_values);

      std::vector<double>               temperature_values (n_q_points);
      std::vector<double>               pressure_values (n_q_points);
      std::vector<std::vector<double> > compositional_values (this->n_compositional_fields(),std::vector<double> (n_q_points));
      std::vector<double>               composition_values_at_q_point (this->n_compositional_fields());
      std::vector<Point<dim> >          quadrature_points(n_q_points);

      typename DoFHandler<dim>::active_cell_iterator
      cell = this->get_dof_handler().begin_active(),
      endc = this->get_dof_handler().end();

      double local_internal_heating_integrals = 0;
      double local_volume = 0;

      // compute the integral quantities by quadrature
      for (; cell!=endc; ++cell)
        if (cell->is_locally_owned())
          {
            fe_values.reinit (cell);
            fe_values[this->introspection().extractors.temperature].get_function_values (this->get_solution(),
                                                                                         temperature_values);
            fe_values[this->introspection().extractors.pressure].get_function_values (this->get_solution(),
                                                                                      pressure_values);
            for (unsigned c=0; c<this->n_compositional_fields(); c++)
              fe_values[this->introspection().extractors.compositional_fields[c]].get_function_values (this->get_solution(),
                  compositional_values[c]);
            quadrature_points=fe_values.get_quadrature_points();

            for (unsigned int q=0; q<n_q_points; ++q)
              {
                for (unsigned c=0; c<this->n_compositional_fields(); c++)
                  composition_values_at_q_point[c]=compositional_values[c][q];
                local_internal_heating_integrals += heating_model.specific_heating_rate(temperature_values[q],
                                                                                        pressure_values[q],
                                                                                        composition_values_at_q_point,
                                                                                        quadrature_points[q])*fe_values.JxW(q);
                local_volume+=fe_values.JxW(q);
              }
          }
      // compute the sum over all processors
      std::vector<double> local_value;
      std::vector<double> global_value;
      local_value.push_back(local_internal_heating_integrals);
      local_value.push_back(local_volume);
      Utilities::MPI::sum (local_value,
                           this->get_mpi_communicator(),
                           global_value);
      double global_internal_heating_integrals=global_value[0];
      double global_volume=global_value[1];

      // finally produce something for the statistics file
      const std::string name1("Average internal heating rate (W/kg) ");
      statistics.add_value (name1, global_internal_heating_integrals/global_volume);
      // also make sure that the other columns filled by the this object
      // all show up with sufficient accuracy and in scientific notation
      statistics.set_precision (name1, 8);
      statistics.set_scientific (name1, true);

      // TODO:
      // Total internal heating rate is not making sense in 2D at the moment,
      // need to put a scale factor to transfer it to 3D.
      const std::string name2("Total internal heating rate (W) ");
      statistics.add_value (name2, global_internal_heating_integrals);
      // also make sure that the other columns filled by the this object
      // all show up with sufficient accuracy and in scientific notation
      statistics.set_precision (name2, 8);
      statistics.set_scientific (name2, true);

      std::ostringstream output;
      output.precision(4);
      output << global_internal_heating_integrals/global_volume << " W/kg, "
             << global_internal_heating_integrals << " W";

      return std::pair<std::string, std::string> ("Internal heating rate (average/total): ",
                                                  output.str());
    }
  }
}


// explicit instantiations
namespace aspect
{
  namespace Postprocess
  {
    ASPECT_REGISTER_POSTPROCESSOR(InternalHeatingStatistics,
                                  "internal heating statistics",
                                  "A postprocessor that computes some statistics about "
                                  "internal heating, averaged by volume. ")
  }
}