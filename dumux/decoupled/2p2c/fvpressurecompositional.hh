// -*- mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
// vi: set et ts=4 sw=4 sts=4:
/*****************************************************************************
 *   Copyright (C) 2010 by Benjamin Faigle                                   *
 *   Copyright (C) 2007-2009 by Bernd Flemisch                               *
 *   Copyright (C) 2008-2009 by Markus Wolff                                 *
 *   Institute of Hydraulic Engineering                                      *
 *   University of Stuttgart, Germany                                        *
 *   email: <givenname>.<name>@iws.uni-stuttgart.de                          *
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
#ifndef DUMUX_FVPRESSURECOMPOSITIONAL_HH
#define DUMUX_FVPRESSURECOMPOSITIONAL_HH

// dumux environment
#include "dumux/common/math.hh"
#include <dumux/decoupled/common/fv/fvpressure.hh>
#include <dumux/decoupled/2p2c/2p2cproperties.hh>


/**
 * @file
 * @brief  Finite Volume Diffusion Model
 * @author Benjamin Faigle, Bernd Flemisch, Jochen Fritz, Markus Wolff
 */

namespace Dumux
{
//! The finite volume model for the solution of the compositional pressure equation
/*! \ingroup multiphase
 *  Provides a Finite Volume implementation for the pressure equation of a gas-liquid
 *  system with two components. An IMPES-like method is used for the sequential
 *  solution of the problem.  Diffusion is neglected, capillarity can be regarded.
 *  Isothermal conditions and local thermodynamic
 *  equilibrium are assumed.  Gravity is included.
 *  \f[
         c_{total}\frac{\partial p}{\partial t} + \sum_{\kappa} \frac{\partial v_{total}}{\partial C^{\kappa}} \nabla \cdot \left( \sum_{\alpha} X^{\kappa}_{\alpha} \varrho_{\alpha} \bf{v}_{\alpha}\right)
          = \sum_{\kappa} \frac{\partial v_{total}}{\partial C^{\kappa}} q^{\kappa},
 *  \f]
 *  where \f$\bf{v}_{\alpha} = - \lambda_{\alpha} \bf{K} \left(\nabla p_{\alpha} + \rho_{\alpha} \bf{g} \right) \f$.
 *  \f$ c_{total} \f$ represents the total compressibility, for constant porosity this yields \f$ - \frac{\partial V_{total}}{\partial p_{\alpha}} \f$,
 *  \f$p_{\alpha} \f$ denotes the phase pressure, \f$ \bf{K} \f$ the absolute permeability, \f$ \lambda_{\alpha} \f$ the phase mobility,
 *  \f$ \rho_{\alpha} \f$ the phase density and \f$ \bf{g} \f$ the gravity constant and \f$ C^{\kappa} \f$ the total Component concentration.
 * See paper SPE 99619 or "Analysis of a Compositional Model for Fluid
 * Flow in Porous Media" by Chen, Qin and Ewing for derivation.
 *
 *  The partial derivatives of the actual fluid volume \f$ v_{total} \f$ are gained by using a secant method.
 *
 * \tparam TypeTag The Type Tag
 */
template<class TypeTag> class FVPressureCompositional
: public FVPressure<TypeTag>
{
    typedef typename GET_PROP_TYPE(TypeTag, PTAG(GridView)) GridView;
    typedef typename GET_PROP_TYPE(TypeTag, PTAG(Scalar)) Scalar;
    typedef typename GET_PROP_TYPE(TypeTag, PTAG(TransportSolutionType)) TransportSolutionType;
    typedef typename GET_PROP_TYPE(TypeTag, PTAG(Problem)) Problem;

    typedef typename GET_PROP_TYPE(TypeTag, PTAG(Indices)) Indices;
    typedef typename GET_PROP_TYPE(TypeTag, PTAG(BoundaryTypes)) BoundaryTypes;

    typedef typename GET_PROP_TYPE(TypeTag, PTAG(FluidSystem)) FluidSystem;
    typedef typename GET_PROP_TYPE(TypeTag, PTAG(FluidState)) FluidState;
    typedef typename GET_PROP_TYPE(TypeTag, PTAG(SpatialParameters))::MaterialLaw MaterialLaw;

    typedef typename GET_PROP_TYPE(TypeTag, PTAG(CellData)) CellData;
    enum
    {
        dim = GridView::dimension, dimWorld = GridView::dimensionworld
    };
    enum
    {
        pw = Indices::pressureW,
        pn = Indices::pressureNW
    };
    enum
    {
        wPhaseIdx = Indices::wPhaseIdx, nPhaseIdx = Indices::nPhaseIdx,
        wCompIdx = Indices::wCompIdx, nCompIdx = Indices::nCompIdx,
        contiWEqIdx = Indices::contiWEqIdx, contiNEqIdx = Indices::contiNEqIdx
    };
    enum
    {
        numPhases = GET_PROP_VALUE(TypeTag, PTAG(NumPhases)),
        numComponents = GET_PROP_VALUE(TypeTag, PTAG(NumComponents))
    };

    // typedefs to abbreviate several dune classes...
    typedef typename GridView::Traits::template Codim<0>::Entity Element;
    typedef typename GridView::template Codim<0>::Iterator ElementIterator;
//    typedef typename GridView::Grid Grid;
//    typedef typename GridView::template Codim<0>::EntityPointer ElementPointer;
    typedef typename GridView::IntersectionIterator IntersectionIterator;

    // convenience shortcuts for Vectors/Matrices
    typedef Dune::FieldVector<Scalar, dimWorld> GlobalPosition;
    typedef Dune::FieldMatrix<Scalar, dim, dim> FieldMatrix;
    typedef Dune::FieldVector<Scalar, numPhases> PhaseVector;
    typedef Dune::FieldVector<Scalar, numComponents> ComponentVector;
    typedef typename GET_PROP_TYPE(TypeTag, PTAG(PrimaryVariables)) PrimaryVariables;

public:
    //the variables object is initialized, non-compositional before and compositional after first pressure calculation
    void initialMaterialLaws(bool compositional);

    //initialization routine to prepare first timestep
    void initialize(bool solveTwice = false);

    //pressure solution routine: update estimate for secants, assemble, solve.
    void update(bool solveTwice = true)
    {
        //pre-transport to estimate update vector
        Scalar dt_estimate = 0.;
        Dune::dinfo << "secant guess"<< std::endl;
        problem_.transportModel().update(-1, dt_estimate, updateEstimate_, false);
        //last argument false in update() makes shure that this is estimate and no "real" transport step
        updateEstimate_ *= problem_.timeManager().timeStepSize();

//        problem_.variables().communicateUpdateEstimate();

        problem_.pressureModel().assemble(false);           Dune::dinfo << "pressure calculation"<< std::endl;
        problem_.pressureModel().solve();

        return;
    }

    //numerical volume derivatives wrt changes in mass, pressure
    void volumeDerivatives(const GlobalPosition&,const Element& ep);

    /*! \name general methods for output */
    //@{
    //! \brief Write data files
     /*  \param name file name */
    template<class MultiWriter>
    void addOutputVtkFields(MultiWriter &writer)
    {
        typedef typename GET_PROP(TypeTag, PTAG(SolutionTypes))::ScalarSolution ScalarSolutionType;
        int size = problem_.gridView().size(0);
        ScalarSolutionType *pressureW = writer.allocateManagedBuffer(size);
        ScalarSolutionType *pressureN = writer.allocateManagedBuffer(size);
        ScalarSolutionType *pC = writer.allocateManagedBuffer(size);
        ScalarSolutionType *saturationW = writer.allocateManagedBuffer(size);

        ScalarSolutionType *densityWetting = writer.allocateManagedBuffer(size);
        ScalarSolutionType *densityNonwetting = writer.allocateManagedBuffer(size);
        ScalarSolutionType *viscosityWetting = writer.allocateManagedBuffer(size);
        ScalarSolutionType *viscosityNonwetting = writer.allocateManagedBuffer(size);
        ScalarSolutionType *mobilityW = writer.allocateManagedBuffer (size);
        ScalarSolutionType *mobilityNW = writer.allocateManagedBuffer (size);

        ScalarSolutionType *massfraction1W = writer.allocateManagedBuffer (size);
        ScalarSolutionType *massfraction1NW = writer.allocateManagedBuffer (size);

        // numerical stuff
        ScalarSolutionType *volErr = writer.allocateManagedBuffer (size);

        for (int i = 0; i < size; i++)
        {
            CellData& cellData = problem_.variables().cellData(i);
            (*pressureW)[i] = cellData.pressure(wPhaseIdx);
            (*pressureN)[i] = cellData.pressure(nPhaseIdx);
            (*pC)[i] = cellData.capillaryPressure();
            (*saturationW)[i] = cellData.saturation(wPhaseIdx);
            (*densityWetting)[i] = cellData.density(wPhaseIdx);
            (*densityNonwetting)[i] = cellData.density(nPhaseIdx);
            (*viscosityWetting)[i] = cellData.viscosity(wPhaseIdx);
            (*viscosityNonwetting)[i] = cellData.viscosity(nPhaseIdx);
            (*mobilityW)[i] = cellData.mobility(wPhaseIdx);
            (*mobilityNW)[i] = cellData.mobility(nPhaseIdx);
            (*massfraction1W)[i] = cellData.massFraction(wPhaseIdx,wCompIdx);
            (*massfraction1NW)[i] = cellData.massFraction(nPhaseIdx,wCompIdx);

            (*volErr)[i] = cellData.volumeError();
        }
        writer.attachCellData(*pressureW, "wetting pressure");
        writer.attachCellData(*pressureN, "nonwetting pressure");
        writer.attachCellData(*pC, "capillary pressure");
        writer.attachCellData(*saturationW, "wetting saturation");

        writer.attachCellData(*densityWetting, "wetting density");
        writer.attachCellData(*densityNonwetting, "nonwetting density");
        writer.attachCellData(*viscosityWetting, "wetting viscosity");
        writer.attachCellData(*viscosityNonwetting, "nonwetting viscosity");
        writer.attachCellData(*mobilityW, "mobility w_phase");
        writer.attachCellData(*mobilityNW, "mobility nw_phase");
        writer.attachCellData(*massfraction1W, "massfraction1 in w_phase");
        writer.attachCellData(*massfraction1NW, "massfraction1NW nw_phase");
        writer.attachCellData(*volErr, "volume Error");

#if DUNE_MINIMAL_DEBUG_LEVEL <= 2
        ScalarSolutionType *pressurePV = writer.allocateManagedBuffer(size);
        ScalarSolutionType *totalConcentration1 = writer.allocateManagedBuffer (size);
        ScalarSolutionType *totalConcentration2 = writer.allocateManagedBuffer (size);

        // add debug stuff
        ScalarSolutionType *numdensityW = writer.allocateManagedBuffer (size);
        ScalarSolutionType *numdensityNW = writer.allocateManagedBuffer (size);
        ScalarSolutionType *errorCorrPtr = writer.allocateManagedBuffer (size);
        ScalarSolutionType *dv_dpPtr = writer.allocateManagedBuffer (size);
        ScalarSolutionType *dV_dC1Ptr = writer.allocateManagedBuffer (size);
        ScalarSolutionType *dV_dC2Ptr = writer.allocateManagedBuffer (size);
        ScalarSolutionType *updEstimate1 = writer.allocateManagedBuffer (size);
        ScalarSolutionType *updEstimate2 = writer.allocateManagedBuffer (size);

        for (int i = 0; i < size; i++)
        {
            CellData& cellData = problem_.variables().cellData(i);
            (*totalConcentration1)[i] = cellData.massConcentration(wCompIdx);
            (*totalConcentration2)[i] = cellData.massConcentration(nCompIdx);

            (*numdensityW)[i] = cellData.numericalDensity(wPhaseIdx);
            (*numdensityNW)[i] = cellData.numericalDensity(nPhaseIdx);
            (*errorCorrPtr)[i] = cellData.errorCorrection();
            (*dv_dpPtr)[i] = cellData.dv_dp();
            (*dV_dC1Ptr)[i] = cellData.dv(wCompIdx);
            (*dV_dC2Ptr)[i] = cellData.dv(nCompIdx);
            (*updEstimate1)[i] = updateEstimate_[0][i];
            (*updEstimate2)[i] = updateEstimate_[1][i];
        }
        *pressurePV = this->pressure();
        writer.attachCellData(*pressurePV, "pressure (Primary Variable");
        writer.attachCellData(*totalConcentration1, "C^w from cellData");
        writer.attachCellData(*totalConcentration2, "C^n from cellData");

        writer.attachCellData(*numdensityW, "numerical density (mass/volume) w_phase");
        writer.attachCellData(*numdensityNW, "numerical density (mass/volume) nw_phase");
        writer.attachCellData(*errorCorrPtr, "Error Correction");
        writer.attachCellData(*dv_dpPtr, "dv_dp");
        writer.attachCellData(*dV_dC1Ptr, "dV_dC1");
        writer.attachCellData(*dV_dC2Ptr, "dV_dC2");
        writer.attachCellData(*updEstimate1, "updEstimate comp 1");
        writer.attachCellData(*updEstimate2, "updEstimate comp 2");
#endif
        return;
    }

    //! \brief Write additional debug info in a special writer
    // used via pseudoTS thorugh the initialization procedure.
    void initializationOutput(double pseudoTS = 0.)
    {
        std::cout << "Writing debug for current time step\n";
        initializationOutputWriter_.beginWrite(problem_.timeManager().time() + pseudoTS);
        addOutputVtkFields(initializationOutputWriter_);

#if DUNE_MINIMAL_DEBUG_LEVEL <= 2
        int size_ = problem_.gridView().size(0);
        // output porosity, permeability
        Dune::BlockVector<Dune::FieldVector<double,1> > *poroPtr = initializationOutputWriter_.allocateManagedBuffer (size_);
        Dune::BlockVector<Dune::FieldVector<double,1> > *permPtr = initializationOutputWriter_.allocateManagedBuffer (size_);

        Dune::BlockVector<Dune::FieldVector<double,1> > poro_(0.), perm_(0.);
        poro_.resize(size_); perm_.resize(size_);
        // iterate over all elements of domain
        for (ElementIterator eIt = problem_.gridView().template begin<0> ();
                eIt != problem_.gridView().template end<0>(); ++eIt)
        {
            // get position, index
            GlobalPosition globalPos = eIt->geometry().center();
            int globalIdx = problem_.variables().index(*eIt);
            poro_[globalIdx] = problem_.spatialParameters().porosity(globalPos, *eIt);
            perm_[globalIdx] = problem_.spatialParameters().intrinsicPermeability(globalPos, *eIt)[0][0];
        }
        *poroPtr = poro_;
        *permPtr = perm_;
        initializationOutputWriter_.attachCellData(*poroPtr, "porosity");
        initializationOutputWriter_.attachCellData(*permPtr, "permeability");
#endif

        initializationOutputWriter_.endWrite();
        return;
    }
    //@}

    //! Constructs a FVPressureCompositional object
    /**
     * \param problem a problem class object
     */
    FVPressureCompositional(Problem& problem) : FVPressure<TypeTag>(problem),
        problem_(problem), initializationOutputWriter_(problem.gridView(),"initOutput2p2c")
    {
        updateEstimate_.resize(GET_PROP_VALUE(TypeTag, PTAG(NumPhases)));
        for  (int i=0; i<GET_PROP_VALUE(TypeTag, PTAG(NumPhases)); i++)
            updateEstimate_[i].resize(problem.gridView().size(0));

        ErrorTermFactor_ = GET_PARAM(TypeTag, Scalar, ErrorTermFactor);
        ErrorTermLowerBound_ = GET_PARAM(TypeTag, Scalar, ErrorTermLowerBound);
        ErrorTermUpperBound_ = GET_PARAM(TypeTag, Scalar, ErrorTermUpperBound);

        if (pressureType != pw && pressureType != pn)
        {
            DUNE_THROW(Dune::NotImplemented, "Pressure type not supported!");
        }
    }
protected:
    TransportSolutionType updateEstimate_; //! Update estimate for changes in volume for the pressure equation
    Problem& problem_;

    //! output for the initialization procedure
    Dumux::VtkMultiWriter<GridView> initializationOutputWriter_;

    Scalar ErrorTermFactor_; //!< Handling of error term: relaxation factor
    Scalar ErrorTermLowerBound_; //!< Handling of error term: lower bound for error dampening
    Scalar ErrorTermUpperBound_; //!< Handling of error term: upper bound for error dampening
    static constexpr int pressureType = GET_PROP_VALUE(TypeTag, PTAG(PressureFormulation)); //!< gives kind of pressure used (\f$ 0 = p_w \f$, \f$ 1 = p_n \f$, \f$ 2 = p_{global} \f$)
};


//! initializes the simulation run
/*!
 * Initializes the simulation to gain the initial pressure field.
 *
 * \param solveTwice flag to determine possible iterations of the initialization process
 */
template<class TypeTag>
void FVPressureCompositional<TypeTag>::initialize(bool solveTwice)
{
    //prepare stiffness Matrix and Vectors
    problem_.pressureModel().initializeMatrix();

    // initialguess: set saturations, determine visco and mobility for initial pressure equation
    // at this moment, the pressure is unknown. Hence, dont regard compositional effects.
    Dune::dinfo << "first saturation guess"<<std::endl; //=J: initialGuess()
        problem_.pressureModel().initialMaterialLaws(false);
            #if DUNE_MINIMAL_DEBUG_LEVEL <= 3
                initializationOutput();
            #endif
    Dune::dinfo << "first pressure guess"<<std::endl;
        problem_.pressureModel().assemble(true);
        problem_.pressureModel().solve();
            #if DUNE_MINIMAL_DEBUG_LEVEL <= 3
                initializationOutput(1e-6);
            #endif
    // update the compositional variables (hence true)
    Dune::dinfo << "first guess for mass fractions"<<std::endl;//= J: transportInitial()
        problem_.pressureModel().initialMaterialLaws(true);

    // perform concentration update to determine secants
    Dune::dinfo << "secant guess"<< std::endl;
        Scalar dt_estimate = 0.;
        problem_.transportModel().update(0., dt_estimate, updateEstimate_, false);
        dt_estimate = std::min ( problem_.timeManager().timeStepSize(), dt_estimate);
        //make sure the right time-step is used by all processes in the parallel case
        if (problem_.gridView().comm().size() > 1)
            dt_estimate = problem_.gridView().comm().min(dt_estimate);

        updateEstimate_ *= dt_estimate;
        //communicate in parallel case
//        problem_.variables().communicateUpdateEstimate();
            #if DUNE_MINIMAL_DEBUG_LEVEL <= 3
                initializationOutput(2e-6);
            #endif
    // pressure calculation
    Dune::dinfo << "second pressure guess"<<std::endl;
        problem_.pressureModel().assemble(false);
        problem_.pressureModel().solve();
            #if DUNE_MINIMAL_DEBUG_LEVEL <= 3
                initializationOutput(3e-6);
            #endif

        // update the compositional variables
        problem_.pressureModel().initialMaterialLaws(true);


    if (solveTwice)
    {
        Dune::BlockVector<Dune::FieldVector<Scalar, 1> > pressureOld(this->pressure());
        Dune::BlockVector<Dune::FieldVector<Scalar, 1> > pressureDiff;
        Scalar pressureNorm = 1.;   //dummy initialization to perform at least 1 iteration
        int numIter = 1;

        while (pressureNorm > 1e-5 && numIter < 10)
        {
            Scalar dt_dummy=0.;    // without this dummy, it will never converge!
            // update for secants
            problem_.transportModel().update(0., dt_dummy, updateEstimate_, false);   Dune::dinfo << "secant guess"<< std::endl;
            updateEstimate_ *= dt_estimate;

            //communicate in parallel case
//            problem_.variables().communicateUpdateEstimate();

            // pressure calculation
            problem_.pressureModel().assemble(false);                 Dune::dinfo << "pressure guess number "<< numIter <<std::endl;
            problem_.pressureModel().solve();
            // update the compositional variables
            problem_.pressureModel().initialMaterialLaws(true);

            pressureDiff = pressureOld;
            pressureDiff -= this->pressure();
            pressureNorm = pressureDiff.infinity_norm();
            pressureOld = this->pressure();
            pressureNorm /= pressureOld.infinity_norm();

            numIter++;
        }
    }
    return;
}

/*!
 *  \brief initializes the fluid distribution and hereby the variables container
 *
 *  This function equals the method initialguess() and transportInitial() in the old model.
 *  It differs from updateMaterialLaws because there are two possible initial conditions:
 *  saturations and concentration.
 *  \param compositional flag that determines if compositional effects are regarded, i.e.
 *      a reasonable pressure field is known.
 */
template<class TypeTag>
void FVPressureCompositional<TypeTag>::initialMaterialLaws(bool compositional)
{
//    problem_.variables().communicateTransportedQuantity();
//    problem_.variables().communicatethis->pressure();

    // iterate through leaf grid an evaluate c0 at cell center
    ElementIterator eItEnd = problem_.gridView().template end<0>();
    ElementIterator eIt = problem_.gridView().template begin<0>();
    for (; eIt != eItEnd; ++eIt)
    {
        // get global coordinate of cell center
        GlobalPosition globalPos = eIt->geometry().center();

        // assign an Index for convenience
        int globalIdx = problem_.variables().index(*eIt);

        // get the temperature
        Scalar temperature_ = problem_.temperatureAtPos(globalPos);
        CellData& cellData = problem_.variables().cellData(globalIdx);
        // acess the fluid state and prepare for manipulation
        FluidState& fluidState = cellData.manipulateFluidState();

        // initial conditions
        PhaseVector pressure(0.);
        Scalar sat_0=0.;

        typename Indices::BoundaryFormulation icFormulation;
        problem_.initialFormulation(icFormulation, *eIt);            // get type of initial condition

        if(!compositional) //means that we do the first approximate guess without compositions
        {
            // phase pressures are unknown, so start with an exemplary
            Scalar exemplaryPressure = problem_.referencePressure(*eIt);
            pressure[wPhaseIdx] = pressure[nPhaseIdx] = this->pressure()[globalIdx] = exemplaryPressure;
            if (icFormulation == Indices::saturation)  // saturation initial condition
            {
                sat_0 = problem_.initSat(*eIt);
                fluidState.satFlash(sat_0, pressure, problem_.spatialParameters().porosity(globalPos, *eIt), temperature_);
            }
            else if (icFormulation == Indices::concentration) // concentration initial condition
            {
                Scalar Z1_0 = problem_.initConcentration(*eIt);
                fluidState.update(Z1_0, pressure, problem_.spatialParameters().porosity(globalPos, *eIt), temperature_);
            }
        }
        else if(compositional)    //means we regard compositional effects since we know an estimate pressure field
        {
            if (icFormulation == Indices::saturation)  // saturation initial condition
            {
                //get saturation, determine pc
                sat_0 = problem_.initSat(*eIt);
                Scalar pc=0.;
                if(GET_PROP_VALUE(TypeTag, PTAG(EnableCapillarity)))
                {
                    pc = MaterialLaw::pC(problem_.spatialParameters().materialLawParams(globalPos, *eIt),
                                    sat_0);
                }
                else
                    pc = 0.;

                //determine phase pressures from primary pressure variable
                switch (pressureType)
                {
                    case pw:
                    {
                        pressure[wPhaseIdx] = this->pressure()[globalIdx];
                        pressure[nPhaseIdx] = this->pressure()[globalIdx] +pc;
                        break;
                    }
                    case pn:
                    {
                        pressure[wPhaseIdx] = this->pressure()[globalIdx]-pc;
                        pressure[nPhaseIdx] = this->pressure()[globalIdx];
                        break;
                    }
                }

                fluidState.satFlash(sat_0, pressure, problem_.spatialParameters().porosity(globalPos, *eIt), temperature_);
            }
            else if (icFormulation == Indices::concentration) // concentration initial condition
            {
                Scalar Z1_0 = problem_.initConcentration(*eIt);
                // If total concentrations are given at the boundary, saturation is unknown.
                // This may affect pc and hence p_alpha and hence again saturation -> iteration.

                // iterations in case of enabled capillary pressure
                if(GET_PROP_VALUE(TypeTag, PTAG(EnableCapillarity)))
                {
                    //start with pc from last TS
                    Scalar pc(cellData.capillaryPressure());

                    int maxiter = 3;
                    //start iteration loop
                    for(int iter=0; iter < maxiter; iter++)
                    {
                        //determine phase pressures from primary pressure variable
                        switch (pressureType)
                        {
                            case pw:
                            {
                                pressure[wPhaseIdx] = this->pressure()[globalIdx];
                                pressure[nPhaseIdx] = this->pressure()[globalIdx] + pc;
                                break;
                            }
                            case pn:
                            {
                                pressure[wPhaseIdx] = this->pressure()[globalIdx] - pc;
                                pressure[nPhaseIdx] = this->pressure()[globalIdx];
                                break;
                            }
                        }

                        //store old pc
                        Scalar oldPc = pc;
                        //update with better pressures
                        fluidState.update(Z1_0, pressure, problem_.spatialParameters().porosity(globalPos, *eIt),
                                            problem_.temperatureAtPos(globalPos));
                        pc = MaterialLaw::pC(problem_.spatialParameters().materialLawParams(globalPos, *eIt),
                                            fluidState.saturation(wPhaseIdx));
                        // TODO: get right criterion, do output for evaluation
                        //converge criterion
                        if (abs(oldPc-pc)<10)
                            iter = maxiter;

                        pc = MaterialLaw::pC(problem_.spatialParameters().materialLawParams(globalPos, *eIt),
                                fluidState.saturation(wPhaseIdx));
                    }
                }
                else  // capillary pressure neglected
                {
                    pressure[wPhaseIdx] = pressure[nPhaseIdx]
                        = this->pressure()[globalIdx];
                    fluidState.update(Z1_0, pressure, problem_.spatialParameters().porosity(globalPos, *eIt), temperature_);
                }

                fluidState.calculateMassConcentration(problem_.spatialParameters().porosity(globalPos, *eIt));

            } //end conc initial condition
        } //end compositional
        problem_.transportModel().totalConcentration(wCompIdx,globalIdx) = fluidState.massConcentration(wCompIdx);
        problem_.transportModel().totalConcentration(nCompIdx,globalIdx) = fluidState.massConcentration(nCompIdx);

        // initialize phase properties not stored in fluidstate
        cellData.setViscosity(wPhaseIdx, FluidSystem::viscosity(fluidState, wPhaseIdx));
        cellData.setViscosity(nPhaseIdx, FluidSystem::viscosity(fluidState, nPhaseIdx));

        // initialize mobilities
        cellData.setMobility(wPhaseIdx, MaterialLaw::krw(problem_.spatialParameters().materialLawParams(globalPos, *eIt), fluidState.saturation(wPhaseIdx))
                    / cellData.viscosity(wPhaseIdx));
        cellData.setMobility(nPhaseIdx, MaterialLaw::krn(problem_.spatialParameters().materialLawParams(globalPos, *eIt), fluidState.saturation(wPhaseIdx))
                    / cellData.viscosity(nPhaseIdx));

        // calculate perimeter used as weighting factor
        if(!compositional)
        {
            // run through all intersections with neighbors
            IntersectionIterator isItEnd = problem_.gridView().template iend(*eIt);
            for (IntersectionIterator isIt = problem_.gridView().template ibegin(*eIt); isIt != isItEnd; ++isIt)
            {
                cellData.perimeter()
                        += isIt->geometry().volume();
            }
            // set dv to zero to prevent output errors
            cellData.dv_dp() = 0.;
            cellData.dv(wPhaseIdx) = 0.;
            cellData.dv(nPhaseIdx) = 0.;
        }
        // all
        cellData.reset();
    }
    return;
}

//! partial derivatives of the volumes w.r.t. changes in total concentration and pressure
/*!
 * This method calculates the volume derivatives via a secant method, where the
 * secants are gained in a pre-computational step via the transport equation and
 * the last TS size.
 * The partial derivatives w.r.t. mass are defined as
 * \f$ \frac{\partial v}{\partial C^{\kappa}} = \frac{\partial V}{\partial m^{\kappa}}\f$
 *
 * \param globalPos The global position of the current element
 * \param ep A pointer to the current element
 */
template<class TypeTag>
void FVPressureCompositional<TypeTag>::volumeDerivatives(const GlobalPosition& globalPos, const Element& element)
{
    // cell index
    int globalIdx = problem_.variables().index(element);

    CellData& cellData = problem_.variables().cellData(globalIdx);

    // get cell temperature
    Scalar temperature_ = problem_.temperatureAtPos(globalPos);

    // initialize an Fluid state for the update
    FluidState updFluidState;

    /**********************************
     * a) get necessary variables
     **********************************/
    //determine phase pressures for flash calculation
    PhaseVector pressure(0.);
    for(int phaseIdx = 0; phaseIdx< numPhases; phaseIdx++)
            pressure[phaseIdx] = cellData.pressure(phaseIdx);

    // mass of components inside the cell
    ComponentVector mass(0.);
    for(int compIdx = 0; compIdx< numComponents; compIdx++)
        mass[compIdx] = cellData.massConcentration(compIdx);

    // actual fluid volume
    // see Fritz 2011 (Dissertation) eq.3.76
    Scalar specificVolume(0.); // = \sum_{\alpha} \nu_{\alpha} / \rho_{\alpha}
    for(int phaseIdx = 0; phaseIdx< numPhases; phaseIdx++)
        specificVolume += cellData.phaseMassFraction(phaseIdx) / cellData.density(phaseIdx);
    Scalar volalt = mass.one_norm() * specificVolume;
        // = \sum_{\kappa} C^{\kappa} + \sum_{\alpha} \nu_{\alpha} / \rho_{\alpha}

    /**********************************
     * b) define increments
     **********************************/
    // increments for numerical derivatives
    ComponentVector massIncrement(0.);
    for(int compIdx = 0; compIdx< numComponents; compIdx++)
    {
        massIncrement[compIdx] = updateEstimate_[compIdx][globalIdx];
        if(fabs(massIncrement[compIdx]) < 1e-8 * cellData.density(compIdx))
            massIncrement[compIdx] = 1e-8* cellData.density(compIdx);   // as phaseIdx = compIdx
    }
    Scalar incp = 1e-2;


    /**********************************
     * c) Secant method for derivatives
     **********************************/

    // numerical derivative of fluid volume with respect to pressure
    PhaseVector p_(incp);
    p_ += pressure;
    Scalar Z1 = mass[0] / mass.one_norm();
    updFluidState.update(Z1,
            p_, problem_.spatialParameters().porosity(globalPos, element), temperature_);

    specificVolume=0.; // = \sum_{\alpha} \nu_{\alpha} / \rho_{\alpha}
    for(int phaseIdx = 0; phaseIdx< numPhases; phaseIdx++)
        specificVolume += updFluidState.phaseMassFraction(phaseIdx) / updFluidState.density(phaseIdx);

    cellData.dv_dp() = ((mass.one_norm() * specificVolume) - volalt) /incp;

    if (cellData.dv_dp()>0)
    {
        // dV_dp > 0 is unphysical: Try inverse increment for secant
        Dune::dinfo << "dv_dp larger 0 at Idx " << globalIdx << " , try and invert secant"<< std::endl;

        p_ -= 2*incp;
        updFluidState.update(Z1,
                    p_, problem_.spatialParameters().porosity(globalPos, element), temperature_);

        specificVolume=0.; // = \sum_{\alpha} \nu_{\alpha} / \rho_{\alpha}
        for(int phaseIdx = 0; phaseIdx< numPhases; phaseIdx++)
            specificVolume += updFluidState.phaseMassFraction(phaseIdx) / updFluidState.density(phaseIdx);
        cellData.dv_dp() = ((mass.one_norm() * specificVolume) - volalt) /incp;

        // dV_dp > 0 is unphysical: Try inverse increment for secant
        if (cellData.dv_dp()>0)
        {
            Dune::dinfo << "dv_dp still larger 0 after inverting secant"<< std::endl;
        }
    }

    // numerical derivative of fluid volume with respect to mass of components
    for (int comp = 0; comp<numComponents; comp++)
    {
        mass[comp] +=  massIncrement[comp];
        Z1 = mass[0] / mass.one_norm();
        updFluidState.update(Z1, pressure, problem_.spatialParameters().porosity(globalPos, element), temperature_);

        specificVolume=0.; // = \sum_{\alpha} \nu_{\alpha} / \rho_{\alpha}
        for(int phaseIdx = 0; phaseIdx< numPhases; phaseIdx++)
            specificVolume += updFluidState.phaseMassFraction(phaseIdx) / updFluidState.density(phaseIdx);

        cellData.dv(comp) = ((mass.one_norm() * specificVolume) - volalt) / massIncrement[comp];
        mass[comp] -= massIncrement[comp];

        //check routines if derivatives are meaningful
        if (isnan(cellData.dv(comp)) || isinf(cellData.dv(comp)) )
        {
            DUNE_THROW(Dune::MathError, "NAN/inf of dV_dm. If that happens in first timestep, try smaller firstDt!");
        }
    }
    cellData.confirmVolumeDerivatives();
}


}//end namespace Dumux
#endif