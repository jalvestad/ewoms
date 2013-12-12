// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
// vi: set et ts=4 sw=4 sts=4:
/*
  Copyright (C) 2008-2013 by Andreas Lauser

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
 *
 * \copydoc Ewoms::FvBaseFluxVariables
 */
#ifndef EWOMS_FV_BASE_FLUX_VARIABLES_HH
#define EWOMS_FV_BASE_FLUX_VARIABLES_HH

#include "fvbaseproperties.hh"

#include <opm/material/Valgrind.hpp>

namespace Ewoms {
/*!
 * \ingroup Discretization
 *
 * \brief Provide the properties at a face which make sense indepentently
 *        of the conserved quantities.
 */
template <class TypeTag>
class FvBaseFluxVariables
{
    typedef typename GET_PROP_TYPE(TypeTag, Scalar) Scalar;
    typedef typename GET_PROP_TYPE(TypeTag, ElementContext) ElementContext;
    typedef typename GET_PROP_TYPE(TypeTag, FluidSystem) FluidSystem;

public:
    /*!
     * \brief Register all run-time parameters for the flux variables.
     */
    static void registerParameters()
    { }

    /*!
     * \brief Update the flux variables for a given sub-control-volume-face.
     *
     * \param elemCtx Reference to the current element context.
     * \param scvfIdx The local index of the sub-control-volume face for which the flux variables should be calculated.
     * \param timeIdx The index used by the time discretization.
     */
    void update(const ElementContext &elemCtx, int scvfIdx, int timeIdx)
    {
        const auto &scvf = elemCtx.stencil(timeIdx).interiorFace(scvfIdx);
        interiorScvIdx_ = scvf.interiorIndex();
        exteriorScvIdx_ = scvf.exteriorIndex();

        extrusionFactor_ =
            (elemCtx.volVars(interiorScvIdx_, timeIdx).extrusionFactor()
             + elemCtx.volVars(exteriorScvIdx_, timeIdx).extrusionFactor()) / 2;
        Valgrind::CheckDefined(extrusionFactor_);
        assert(extrusionFactor_ > 0);
    }


    /*!
     * \brief Update the flux variables for a given boundary face.
     *
     * \param context Reference to the current execution context.
     * \param bfIdx The local index of the boundary face for which the flux variables should be calculated.
     * \param timeIdx The index used by the time discretization.
     * \param fluidState The FluidState on the domain boundary.
     * \param paramCache The FluidSystem's parameter cache.
     */
    template <class Context, class FluidState>
    void updateBoundary(const Context &context,
                        int bfIdx,
                        int timeIdx,
                        const FluidState &fluidState,
                        typename FluidSystem::ParameterCache &paramCache)
    {
        int dofIdx = context.interiorScvIndex(bfIdx, timeIdx);
        interiorScvIdx_ = dofIdx;
        exteriorScvIdx_ = dofIdx;

        extrusionFactor_ = context.volVars(bfIdx, timeIdx).extrusionFactor();
        Valgrind::CheckDefined(extrusionFactor_);
        assert(extrusionFactor_ > 0);
    }

    /*!
     * \brief Returns th extrusion factor for the sub-control volume face
     */
    Scalar extrusionFactor() const
    { return extrusionFactor_; }

    /*!
     * \brief Return the local index of the control volume which is on
     *        the "interior" of the sub-control volume face.
     */
    short interiorIndex() const
    { return interiorScvIdx_; }

    /*!
     * \brief Return the local index of the control volume which is on
     *        the "exterior" of the sub-control volume face.
     */
    short exteriorIndex() const
    { return exteriorScvIdx_; }

private:
    // local indices of the interior and the exterior sub-control volumes
    short interiorScvIdx_;
    short exteriorScvIdx_;

    Scalar extrusionFactor_;
};

} // namespace Ewoms

#endif
