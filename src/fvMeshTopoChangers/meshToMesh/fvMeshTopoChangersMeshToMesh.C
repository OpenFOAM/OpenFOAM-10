/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     | Website:  https://openfoam.org
    \\  /    A nd           | Copyright (C) 2022 OpenFOAM Foundation
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

\*---------------------------------------------------------------------------*/

#include "fvMeshTopoChangersMeshToMesh.H"
#include "polyTopoChangeMap.H"
#include "volFields.H"
#include "surfaceInterpolate.H"
#include "pointFields.H"
#include "meshToMeshAdjustTimeStepFunctionObject.H"
#include "meshToMesh.H"
#include "cellVolumeWeightMethod.H"
#include "MeshToMeshMapGeometricFields.H"
#include "polyMeshMap.H"
#include "addToRunTimeSelectionTable.H"

// * * * * * * * * * * * * * * Static Data Members * * * * * * * * * * * * * //

namespace Foam
{
namespace fvMeshTopoChangers
{
    defineTypeNameAndDebug(meshToMesh, 0);
    addToRunTimeSelectionTable(fvMeshTopoChanger, meshToMesh, fvMesh);
}
}


// * * * * * * * * * * * * Private Member Functions  * * * * * * * * * * * * //

void Foam::fvMeshTopoChangers::meshToMesh::readDict()
{
    const dictionary& meshToMeshDict(dict());

    meshToMeshDict.lookup("times") >> times_;

    meshToMeshDict.lookup("timeDelta") >> timeDelta_;

    forAll(times_, i)
    {
        timeIndices_.insert(label(times_[i]/timeDelta_));
    }
}


Foam::word Foam::fvMeshTopoChangers::meshToMesh::Uname
(
    const surfaceVectorField& Uf
) const
{
    const word UfName(Uf.member());

    return
        IOobject::groupName
        (
            UfName.back() == 'f'
          ? word(UfName(UfName.size() - 1))
          : word::null,
            Uf.group()
        );
}


void Foam::fvMeshTopoChangers::meshToMesh::interpolateUfs()
{
    // Interpolate U to Uf
    HashTable<surfaceVectorField*> Ufs
    (
        mesh().lookupClass<surfaceVectorField>()
    );

    forAllIter(HashTable<surfaceVectorField*>, Ufs, iter)
    {
        surfaceVectorField& Uf = *iter();

        const word Uname(this->Uname(Uf));

        if (Uname != word::null)
        {
            Uf.reset
            (
                fvc::interpolate
                (
                    mesh().lookupObject<volVectorField>(Uname)
                )
            );
        }
    }
}


// * * * * * * * * * * * * * * * * Constructors  * * * * * * * * * * * * * * //

Foam::fvMeshTopoChangers::meshToMesh::meshToMesh(fvMesh& mesh)
:
    fvMeshTopoChanger(mesh),
    timeIndex_(-1)
{
    // Read static part of dictionary
    readDict();
}


// * * * * * * * * * * * * * * * * Destructor  * * * * * * * * * * * * * * * //

Foam::fvMeshTopoChangers::meshToMesh::~meshToMesh()
{}


// * * * * * * * * * * * * * * * Member Functions  * * * * * * * * * * * * * //

bool Foam::fvMeshTopoChangers::meshToMesh::update()
{
    if (timeIndex_ == -1)
    {
        const_cast<Time&>(mesh().time()).functionObjects().append
        (
            new functionObjects::meshToMeshAdjustTimeStepFunctionObject
            (
                "meshToMeshAdjustTimeStep",
                mesh().time(),
                dict()
            )
        );
    }

    // Only refine on the first call in a time-step
    if (timeIndex_ != mesh().time().timeIndex())
    {
        timeIndex_ = mesh().time().timeIndex();
    }
    else
    {
        mesh().topoChanging(false);

        return false;
    }

    bool hasChanged = false;

    const scalar userTime = mesh().time().userTimeValue();

    if (timeIndices_.found((userTime + timeDelta_/2)/timeDelta_))
    {
        const word meshDir = "meshToMesh_" + mesh().time().timeName(userTime);

        Info << "Mapping to mesh " << meshDir << endl;

        hasChanged = true;

        fvMesh newMesh
        (
            IOobject
            (
                meshDir,
                mesh().time().constant(),
                mesh().time(),
                IOobject::MUST_READ
            ),
            false
        );

        Foam::meshToMesh mapper
        (
            mesh(),
            newMesh,
            cellVolumeWeightMethod::typeName
        );

        mesh().reset(newMesh);

        // Map all the volFields in the objectRegistry
        #define mapVolFieldType(Type, nullArg)                                 \
            MeshToMeshMapVolFields<Type>(mapper);
        FOR_ALL_FIELD_TYPES(mapVolFieldType);

        // Set all the surfaceFields in the objectRegistry to NaN
        #define NaNSurfaceFieldType(Type, nullArg)                             \
            NaNGeometricFields                                                 \
            <Type, fvsPatchField, surfaceMesh, fvPatchFieldMapper>(mapper);
        FOR_ALL_FIELD_TYPES(NaNSurfaceFieldType);

        // Set all the pointFields in the objectRegistry to NaN
        #define NaNPointFieldType(Type, nullArg)                               \
            NaNGeometricFields                                                 \
            <Type, pointPatchField, pointMesh, pointPatchFieldMapper>(mapper);
        FOR_ALL_FIELD_TYPES(NaNPointFieldType);

        // Interpolate U's to Uf's
        interpolateUfs();

        polyMeshMap map;
        mesh().mapMesh(map);
    }

    mesh().topoChanging(hasChanged);

    return hasChanged;
}


void Foam::fvMeshTopoChangers::meshToMesh::topoChange
(
    const polyTopoChangeMap& map
)
{}


void Foam::fvMeshTopoChangers::meshToMesh::mapMesh(const polyMeshMap& map)
{}


void Foam::fvMeshTopoChangers::meshToMesh::distribute
(
    const polyDistributionMap& map
)
{}


// ************************************************************************* //