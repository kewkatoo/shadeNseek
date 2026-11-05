#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ShadeSunSubsystem.generated.h"

class ADirectionalLight;

/**
 * The single source of truth for "is this point in direct sunlight?".
 *
 * Gameplay shade is physical: a point is lit when nothing blocks the Sunlight trace channel between it and the sun
 * (the level's atmosphere directional light). Every shadow you see from buildings, crates, awnings, pots and
 * the bell tower is therefore also gameplay shade - including shadows that move.
 */
UCLASS()
class SHADENSEEK_API UShadeSunSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UShadeSunSubsystem* Get(const UObject* WorldContext);

	ADirectionalLight* GetSun() const;

	/** Unit vector pointing from the scene towards the sun. */
	FVector GetToSun() const;

	bool IsSunlit(const FVector& Point, const AActor* IgnoreA = nullptr, const AActor* IgnoreB = nullptr) const;

	/** Fraction (0..1) of the given points that are lit. */
	float SunlitFraction(const TArray<FVector>& Points, const AActor* IgnoreA = nullptr, const AActor* IgnoreB = nullptr) const;

	/** Debug/QA: top-down PNG of the play area (warm = lit ground, blue = shade, red dots = TargetPoints tagged AP_*). */
	bool WriteSunMap(const FBox2D& Area, float StepCm, float TopZ, const FString& Path) const;

	UPROPERTY(EditAnywhere, Category = "Sun")
	float TraceDistance = 20000.f;

private:
	mutable TWeakObjectPtr<ADirectionalLight> Sun;
};
