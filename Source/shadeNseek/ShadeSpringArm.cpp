#include "ShadeSpringArm.h"

FVector UShadeSpringArm::BlendLocations(const FVector& DesiredArmLocation, const FVector& TraceHitLocation, bool bHitSomething, float DeltaTime)
{
	// PreviousArmOrigin holds this frame's arm origin: the spring arm stores it right before the sweep.
	const FVector Origin = PreviousArmOrigin;
	const FVector Span = DesiredArmLocation - Origin;
	const float Full = Span.Size();
	if (Full < KINDA_SMALL_NUMBER)
	{
		return DesiredArmLocation;
	}
	const float Dt = FMath::Clamp(DeltaTime, 0.f, 0.1f);
	const float Floor = FMath::Min(MinLength, Full);

	// What the probe asks for this frame. A hit right at the origin is a start-penetrating sweep, not a wall
	// between Shade and the camera: ignore it.
	float Raw = Full;
	if (bHitSomething)
	{
		const float HitDist = FVector::Dist(Origin, TraceHitLocation);
		if (HitDist >= Floor * 0.5f)
		{
			Raw = FMath::Clamp(HitDist, Floor, Full);
		}
	}

	// Hold the shortest recent request for a moment: edges that are hit on alternate frames give a steady target.
	HeldAge += Dt;
	if (HeldWant < 0.f || Raw <= HeldWant || HeldAge >= HoldTime)
	{
		HeldWant = Raw;
		HeldAge = 0.f;
	}
	const float Want = FMath::Clamp(FMath::Min(HeldWant, Full), Floor, Full);

	if (SmoothedLength < 0.f)
	{
		SmoothedLength = Want;
	}
	const float Rate = Want < SmoothedLength ? InSpeed : OutSpeed;
	SmoothedLength = FMath::Lerp(SmoothedLength, Want, 1.f - FMath::Exp(-Rate * Dt));
	// Never further out than the clear length, and never far past a (held, so steady) hit.
	SmoothedLength = FMath::Clamp(FMath::Min(SmoothedLength, Want + 25.f), Floor, Full);
	return Origin + Span / Full * SmoothedLength;
}
