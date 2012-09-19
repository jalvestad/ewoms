// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
// vi: set et ts=4 sw=4 sts=4:
/*****************************************************************************
 *   Copyright (C) 2012 by Andreas Lauser                                    *
 *                                                                           *
 *   This program is free software: you can redistribute it and/or modify    *
 *   it under the terms of the GNU General Public License as published by    *
 *   the Free Software Foundation, either version 2 of the License, or       *
 *   (at your option) any later version.                                     *
 *                                                                           *
 *   This program is distributed in the hope that it will be useful,         *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 *   GNU General Public License for more details.                            *
 *                                                                           *
 *   You should have received a copy of the GNU General Public License       *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.   *
 *****************************************************************************/
/*!
 * \file
 *
 * \copydoc Dumux::BlackOilVolumeVariables
 */
#ifndef DUMUX_BLACK_OIL_VOLUME_VARIABLES_HH
#define DUMUX_BLACK_OIL_VOLUME_VARIABLES_HH

#include "blackoilproperties.hh"

#include <dumux/boxmodels/common/boxvolumevariables.hh>
#include <dumux/material/fluidstates/compositionalfluidstate.hh>

#include <dune/common/fvector.hh>

namespace Dumux {
/*!
 * \ingroup BlackOilBoxModel
 * \ingroup BoxVolumeVariables
 *
 * \brief Contains the quantities which are are constant within a
 *        finite volume in the black-oil model.
 */
template <class TypeTag>
class BlackOilVolumeVariables
    : public BoxVolumeVariables<TypeTag>
    , public GET_PROP_TYPE(TypeTag, VelocityModule)::VelocityVolumeVariables

{
    typedef BoxVolumeVariables<TypeTag> ParentType;

    typedef typename GET_PROP_TYPE(TypeTag, Scalar) Scalar;
    typedef typename GET_PROP_TYPE(TypeTag, FluidSystem) FluidSystem;
    typedef typename GET_PROP_TYPE(TypeTag, MaterialLaw) MaterialLaw;
    typedef typename GET_PROP_TYPE(TypeTag, ElementContext) ElementContext;
    typedef typename GET_PROP_TYPE(TypeTag, BlackOilFluidState) FluidState;
    typedef typename GET_PROP_TYPE(TypeTag, Indices) Indices;
    typedef typename GET_PROP_TYPE(TypeTag, GridView) GridView;
    typedef typename GET_PROP_TYPE(TypeTag, VelocityModule) VelocityModule;

    enum { numPhases = GET_PROP_VALUE(TypeTag, NumPhases) };
    enum { numComponents = GET_PROP_VALUE(TypeTag, NumComponents) };
    enum { saturation0Idx = Indices::saturation0Idx };
    enum { wCompIdx = FluidSystem::wCompIdx };
    enum { oCompIdx = FluidSystem::oCompIdx };
    enum { gCompIdx = FluidSystem::gCompIdx };
    enum { wPhaseIdx = FluidSystem::wPhaseIdx };
    enum { oPhaseIdx = FluidSystem::oPhaseIdx };
    enum { gPhaseIdx = FluidSystem::gPhaseIdx };
    enum { dimWorld = GridView::dimensionworld };

    typedef Dune::FieldMatrix<Scalar, dimWorld, dimWorld> DimMatrix;

    typedef typename VelocityModule::VelocityVolumeVariables VelocityVolumeVariables;

public:
    /*!
     * \copydoc BoxVolumeVariables::update
     */
    void update(const ElementContext &elemCtx,
                int scvIdx,
                int timeIdx)
    {
        ParentType::update(elemCtx,
                           scvIdx,
                           timeIdx);

        fluidState_.setTemperature(elemCtx.problem().temperature(elemCtx, scvIdx, timeIdx));

        // material law parameters
        typedef typename GET_PROP_TYPE(TypeTag, MaterialLaw) MaterialLaw;
        const auto &problem = elemCtx.problem();
        const typename MaterialLaw::Params &materialParams =
            problem.materialLawParams(elemCtx, scvIdx, timeIdx);
        const auto &priVars = elemCtx.primaryVars(scvIdx, timeIdx);

        // update the saturations
        Scalar sumSat = 0.0;
        for (int phaseIdx = 0; phaseIdx < numPhases - 1; ++ phaseIdx) {
            fluidState_.setSaturation(phaseIdx, priVars[saturation0Idx + phaseIdx]);
            sumSat += priVars[saturation0Idx + phaseIdx];
        }
        fluidState_.setSaturation(numPhases - 1, 1 - sumSat);
        
        // update the pressures
        Scalar p0 = priVars[0];
        Scalar pC[numPhases];
        MaterialLaw::capillaryPressures(pC, materialParams, fluidState_);
        for (int phaseIdx = 0; phaseIdx < numPhases; ++ phaseIdx) {
            fluidState_.setPressure(phaseIdx, p0 + (pC[phaseIdx] - pC[0]));
        }
        
        // update phase compositions. first, set everything to 0, then
        // make the gas/water phases consist of only the gas/water
        // components and calculate the composition of the liquid oil
        // phase from the gas formation factor plus the gas/oil
        // formation volume factors and the reference densities
        for (int phaseIdx = 0; phaseIdx < numPhases; ++ phaseIdx)
            for (int compIdx = 0; compIdx < numComponents; ++compIdx)
                fluidState_.setMoleFraction(phaseIdx, compIdx, 0.0);
        // set composition of gas and water phases
        fluidState_.setMoleFraction(gPhaseIdx, gCompIdx, 1.0);
        fluidState_.setMoleFraction(wPhaseIdx, wCompIdx, 1.0);

        // retrieve the relevant black-oil parameters from the fluid
        // system.
        Scalar pBub = FluidSystem::bubblePressure();
        Scalar p = fluidState_.pressure(oPhaseIdx);
        if (fluidState_.pressure(oPhaseIdx) > pBub)
            p = pBub;
        Scalar Bg = FluidSystem::gasFormationVolumeFactor(p);
        Scalar Bo = FluidSystem::oilFormationVolumeFactor(p);
        Scalar Rs = FluidSystem::gasFormationFactor(p);
        Scalar rhoo = FluidSystem::surfaceDensity(oPhaseIdx)/Bo;
        Scalar rhorefg = FluidSystem::surfaceDensity(gPhaseIdx);
        Scalar MG = FluidSystem::molarMass(gPhaseIdx);
        Scalar MO = FluidSystem::molarMass(oPhaseIdx);
        
        // calculate composition of oil phase in terms of mass
        // fractions.
        Scalar XoG = Rs*rhorefg / rhoo;
        Scalar XoO = 1 - XoG;
        
        assert(XoG >= 0 && XoO >= 0);

        // convert to mole fractions
        Scalar avgMolarMass = MO*MG/(MG + XoO*(MO - MG));
        Scalar xoG = XoG*avgMolarMass/MG;
        Scalar xoO = 1 - XoG;
        
        // finally set the oil-phase composition. yeah!
        fluidState_.setMoleFraction(oPhaseIdx, gCompIdx, xoG);
        fluidState_.setMoleFraction(oPhaseIdx, oCompIdx, xoO);

        // handle undersaturated oil
        if (fluidState_.pressure(oPhaseIdx) > pBub)
            rhoo += FluidSystem::oilCompressibility() * (fluidState_.pressure(oPhaseIdx) - pBub);

        typedef typename GET_PROP_TYPE(TypeTag, FluidSystem) FluidSystem;
        typename FluidSystem::ParameterCache paramCache;
        paramCache.updateAll(fluidState_);

        for (int phaseIdx = 0; phaseIdx < numPhases; ++phaseIdx) {
            // compute and set the viscosity
            Scalar mu = FluidSystem::viscosity(fluidState_, paramCache, phaseIdx);
            fluidState_.setViscosity(phaseIdx, mu);
        }

        // set the phase densities
        fluidState_.setDensity(oPhaseIdx, rhoo);
        fluidState_.setDensity(wPhaseIdx, FluidSystem::density(fluidState_, paramCache, wPhaseIdx));
        fluidState_.setDensity(gPhaseIdx, rhorefg/Bg);
        
        // calculate relative permeabilities
        MaterialLaw::relativePermeabilities(relativePermeability_, materialParams, fluidState_);
        Valgrind::CheckDefined(relativePermeability_);

        // retrieve the porosity from the problem
        porosity_ = problem.porosity(elemCtx, scvIdx, timeIdx);

        // intrinsic permeability
        intrinsicPerm_ = problem.intrinsicPermeability(elemCtx, scvIdx, timeIdx);

        // update the quantities specific for the velocity model
        VelocityVolumeVariables::update_(elemCtx, scvIdx, timeIdx);
    }

    /*!
     * \copydoc ImmiscibleVolumeVariables::fluidState
     */
    const FluidState &fluidState() const
    { return fluidState_; }

    /*!
     * \copydoc ImmiscibleVolumeVariables::intrinsicPermeability
     */
    const DimMatrix &intrinsicPermeability() const
    { return intrinsicPerm_; }

    /*!
     * \copydoc ImmiscibleVolumeVariables::relativePermeability
     */
    Scalar relativePermeability(int phaseIdx) const
    { return relativePermeability_[phaseIdx]; }

    /*!
     * \copydoc ImmiscibleVolumeVariables::mobility
     */
    Scalar mobility(int phaseIdx) const
    { return relativePermeability(phaseIdx)/fluidState().viscosity(phaseIdx); }

    /*!
     * \copydoc ImmiscibleVolumeVariables::porosity
     */
    Scalar porosity() const
    { return porosity_; }

private:
    FluidState fluidState_;
    Scalar porosity_;
    DimMatrix intrinsicPerm_;
    Scalar relativePermeability_[numPhases];
};

}

#endif
