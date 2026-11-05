#include "ShadeAssets.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Sound/SoundBase.h"
#include "Components/AudioComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	template<typename T>
	T* LoadQuiet(const FString& Path)
	{
		return Cast<T>(StaticLoadObject(T::StaticClass(), nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet));
	}

	FString ObjectPath(const TCHAR* Folder, const TCHAR* Name)
	{
		return FString::Printf(TEXT("/Game/Shade/%s/%s.%s"), Folder, Name, Name);
	}
}

UStaticMesh* ShadeAssets::Mesh(const TCHAR* Name)
{
	return LoadQuiet<UStaticMesh>(ObjectPath(TEXT("Meshes"), Name));
}

UStaticMesh* ShadeAssets::PropMesh(const TCHAR* Folder, const TCHAR* Name)
{
	return LoadQuiet<UStaticMesh>(FString::Printf(TEXT("/Game/Shade/Props/%s/%s.%s"), Folder, Name, Name));
}

USkeletalMesh* ShadeAssets::Skeletal(const TCHAR* Name)
{
	return LoadQuiet<USkeletalMesh>(ObjectPath(TEXT("Characters"), Name));
}

UMaterialInterface* ShadeAssets::Material(const TCHAR* Name)
{
	return LoadQuiet<UMaterialInterface>(ObjectPath(TEXT("Materials"), Name));
}

USoundBase* ShadeAssets::Sound(const TCHAR* Name)
{
	return LoadQuiet<USoundBase>(ObjectPath(TEXT("Audio"), Name));
}

UMaterialInstanceDynamic* ShadeAssets::MakeMID(UPrimitiveComponent* Comp, int32 Slot, const TCHAR* MaterialName)
{
	if (!Comp)
	{
		return nullptr;
	}
	UMaterialInterface* Parent = MaterialName ? Material(MaterialName) : Comp->GetMaterial(Slot);
	if (!Parent)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(Parent, Comp);
	Comp->SetMaterial(Slot, MID);
	return MID;
}

UMaterialInstanceDynamic* ShadeAssets::SetCatMaterial(UPrimitiveComponent* Comp, const TCHAR* SlotName, int32 Fallback, const TCHAR* MaterialName)
{
	if (!Comp)
	{
		return nullptr;
	}
	int32 Slot = Comp->GetMaterialIndex(FName(SlotName));
	if (Slot == INDEX_NONE)
	{
		Slot = Fallback;
	}
	if (Slot < 0 || Slot >= Comp->GetNumMaterials())
	{
		return nullptr;
	}
	return MakeMID(Comp, Slot, MaterialName);
}

void ShadeAssets::Play2D(const UObject* WorldContext, const TCHAR* Name, float Volume, float Pitch)
{
	if (USoundBase* S = Sound(Name))
	{
		UGameplayStatics::PlaySound2D(WorldContext, S, Volume, Pitch);
	}
}

void ShadeAssets::PlayAt(const UObject* WorldContext, const TCHAR* Name, const FVector& Location, float Volume, float Pitch)
{
	if (USoundBase* S = Sound(Name))
	{
		UGameplayStatics::PlaySoundAtLocation(WorldContext, S, Location, Volume, Pitch);
	}
}

void ShadeAssets::PlayVariantAt(const UObject* WorldContext, const TCHAR* Name, int32 Count, const FVector& Location, float Volume, float PitchJitter)
{
	const FString Variant = FString::Printf(TEXT("%s_%02d"), Name, FMath::RandRange(1, FMath::Max(1, Count)));
	PlayAt(WorldContext, *Variant, Location, Volume, 1.f + FMath::FRandRange(-PitchJitter, PitchJitter));
}

UAudioComponent* ShadeAssets::Loop(USceneComponent* AttachTo, const TCHAR* Name, float Volume, bool bAutoPlay)
{
	USoundBase* S = Sound(Name);
	if (!S || !AttachTo)
	{
		return nullptr;
	}
	UAudioComponent* AC = UGameplayStatics::SpawnSoundAttached(S, AttachTo, NAME_None, FVector::ZeroVector,
		EAttachLocation::KeepRelativeOffset, false, Volume, 1.f, 0.f, nullptr, nullptr, /*bAutoDestroy*/ false);
	if (AC && !bAutoPlay)
	{
		AC->Stop();
	}
	return AC;
}
