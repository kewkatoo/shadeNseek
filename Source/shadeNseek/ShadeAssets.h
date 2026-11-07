#pragma once

#include "CoreMinimal.h"

class UStaticMesh;
class USkeletalMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class USoundBase;
class UAudioComponent;
class USceneComponent;
class UPrimitiveComponent;

/**
 * Central lookup for project assets by short name. Everything lives under /Game/Shade and is always cooked:
 *   Meshes/<Name>, Materials/<Name>, Audio/<Name>, Characters/<Name>.
 */
namespace ShadeAssets
{
	SHADENSEEK_API UStaticMesh* Mesh(const TCHAR* Name);
	SHADENSEEK_API UStaticMesh* PropMesh(const TCHAR* Folder, const TCHAR* Name);
	SHADENSEEK_API USkeletalMesh* Skeletal(const TCHAR* Name);
	SHADENSEEK_API UMaterialInterface* Material(const TCHAR* Name);
	/** Sounds are cached and rooted on first use, so a footstep never reloads one from disk mid-game. */
	SHADENSEEK_API USoundBase* Sound(const TCHAR* Name);
	/** Loads every sound under /Game/Shade/Audio up front (StartPlay), instead of hitching on first play. */
	SHADENSEEK_API void PreloadAudio();

	/** Creates a dynamic instance of the named material (or of the slot's current material) on the given slot. */
	SHADENSEEK_API UMaterialInstanceDynamic* MakeMID(UPrimitiveComponent* Comp, int32 Slot = 0, const TCHAR* MaterialName = nullptr);
	/**
	 * Cat mesh slots (M_CatBody 0, M_CatEye 1, M_CatWhisker 2, M_CatShell 3): puts a dynamic instance of the named
	 * material on the slot imported under SlotName (falls back to the authored index).
	 */
	SHADENSEEK_API UMaterialInstanceDynamic* SetCatMaterial(UPrimitiveComponent* Comp, const TCHAR* SlotName, int32 Fallback, const TCHAR* MaterialName);

	SHADENSEEK_API void Play2D(const UObject* WorldContext, const TCHAR* Name, float Volume = 1.f, float Pitch = 1.f);
	SHADENSEEK_API void PlayAt(const UObject* WorldContext, const TCHAR* Name, const FVector& Location, float Volume = 1.f, float Pitch = 1.f);
	/** Plays one of Name_01..Name_Count at random. */
	SHADENSEEK_API void PlayVariantAt(const UObject* WorldContext, const TCHAR* Name, int32 Count, const FVector& Location, float Volume = 1.f, float PitchJitter = 0.06f);
	/** Persistent looping audio component (the sound asset must be set to loop). */
	SHADENSEEK_API UAudioComponent* Loop(USceneComponent* AttachTo, const TCHAR* Name, float Volume = 1.f, bool bAutoPlay = true);
}
