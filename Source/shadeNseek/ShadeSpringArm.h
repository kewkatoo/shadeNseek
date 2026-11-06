#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SpringArmComponent.h"
#include "ShadeSpringArm.generated.h"

/**
 * Follow-camera boom that never pops. The stock spring arm jumps straight to every collision hit and straight back
 * when it clears, so walking past pots, shutters or the passage arch made the whole view jerk in and out. Here the
 * arm pulls in quickly (so the camera does not sit inside a wall) and eases back out slowly.
 */
UCLASS(ClassGroup = (Shade))
class SHADENSEEK_API UShadeSpringArm : public USpringArmComponent
{
	GENERATED_BODY()

public:
	/** How fast the arm shortens towards a hit (1/s, exponential). */
	float InSpeed = 16.f;
	/** How fast it lengthens again once clear (1/s, exponential). */
	float OutSpeed = 2.2f;

protected:
	virtual FVector BlendLocations(const FVector& DesiredArmLocation, const FVector& TraceHitLocation, bool bHitSomething, float DeltaTime) override;

	float SmoothedLength = -1.f;
};
