#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SpringArmComponent.h"
#include "ShadeSpringArm.generated.h"

/**
 * Follow-camera boom that never pops. The stock spring arm jumps straight to every collision hit and straight back
 * when it clears, so walking past pots, shutters or the passage arch made the whole view jerk in and out. Here:
 *  - the arm pulls in quickly (so the camera does not sit inside a wall) and eases back out slowly;
 *  - a pull-in is held for a moment, so a probe that grazes an edge on alternate frames cannot pump the camera;
 *  - hits closer than MinLength are ignored: they mean the probe started against something (a hop into the vault
 *    ceiling, the cart handle beside Shade) and used to drop the camera inside the cat, which then "vanished".
 */
UCLASS(ClassGroup = (Shade))
class SHADENSEEK_API UShadeSpringArm : public USpringArmComponent
{
	GENERATED_BODY()

public:
	/** How fast the arm shortens towards a hit (1/s, exponential). */
	float InSpeed = 14.f;
	/** How fast it lengthens again once clear (1/s, exponential). */
	float OutSpeed = 2.f;
	/** The camera never comes closer to the pivot than this. */
	float MinLength = 85.f;
	/** A pull-in target is kept for this long after the probe last asked for it (s). */
	float HoldTime = 0.25f;

protected:
	virtual FVector BlendLocations(const FVector& DesiredArmLocation, const FVector& TraceHitLocation, bool bHitSomething, float DeltaTime) override;

	float SmoothedLength = -1.f;
	float HeldWant = -1.f;
	float HeldAge = 0.f;
};
