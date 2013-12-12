// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
// vi: set et ts=4 sw=4 sts=4:
/*
  Copyright (C) 2009-2013 by Andreas Lauser

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/
/*!
 * \file
 * \ingroup PvsModel
 *
 * \brief Declares the properties required for the compositional
 *        multi-phase primary variable switching model.
 */
#ifndef EWOMS_PVS_PROPERTIES_HH
#define EWOMS_PVS_PROPERTIES_HH

#include <ewoms/models/common/multiphasebasemodel.hh>

#include <ewoms/vtk/vtkcompositionmodule.hh>
#include <ewoms/vtk/vtkphasepresencemodule.hh>
#include <ewoms/vtk/vtkdiffusionmodule.hh>
#include <ewoms/vtk/vtkenergymodule.hh>

namespace Opm {
namespace Properties {
//! The type tag for the isothermal single phase problems
NEW_TYPE_TAG(PvsModel, INHERITS_FROM(MultiPhaseBaseModel, VtkPhasePresence, VtkComposition, VtkEnergy, VtkDiffusion));

NEW_PROP_TAG(EnableEnergy); //!< Specifies whether energy is considered as a conservation quantity or not
NEW_PROP_TAG(EnableDiffusion); //!< Enable diffusive fluxes?

NEW_PROP_TAG(PvsVerbosity); //!< The verbosity of the model (0 -> do not print anything, 2 -> spam stdout a lot)
NEW_PROP_TAG(PvsPressureBaseWeight); //!< The basis value for the weight of the pressure primary variable
NEW_PROP_TAG(PvsSaturationsBaseWeight); //!< The basis value for the weight of the saturation primary variables
NEW_PROP_TAG(PvsMoleFractionsBaseWeight); //!< The basis value for the weight of the mole fraction primary variables
} // namespace Properties
} // namespace Opm

#endif
