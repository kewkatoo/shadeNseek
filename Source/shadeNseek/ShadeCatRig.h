#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ShadeTypes.h"
#include "ShadeCatRig.generated.h"

class UPoseableMeshComponent;

UENUM()
enum class ECatPose : uint8
{
	Locomote,
	Push,
	Sit,
	Sleep,
	Count UMETA(Hidden)
};

namespace CatBone
{
	enum Type : int32
	{
		Root, Pelvis, Spine1, Spine2, Neck, Head, EarL, EarR,
		UArmL, FArmL, HandL, UArmR, FArmR, HandR,
		ThighL, CalfL, FootL, ThighR, CalfR, FootR,
		Tail1, Tail2, Tail3, Tail4, Tail5,
		Count
	};
}

/** One evaluated pose: per-bone (pitch, yaw, roll) in degrees in cat space + pelvis offset (cm, cat space F/R/U). */
struct FCatPoseSample
{
	FVector Ang[CatBone::Count];
	FVector Pelvis = FVector::ZeroVector;

	FCatPoseSample() { Reset(); }
	void Reset()
	{
		for (FVector& A : Ang) { A = FVector::ZeroVector; }
		Pelvis = FVector::ZeroVector;
	}
	void AddScaled(const FCatPoseSample& O, float W)
	{
		for (int32 i = 0; i < CatBone::Count; ++i) { Ang[i] += O.Ang[i] * W; }
		Pelvis += O.Pelvis * W;
	}
};

/**
 * Procedural quadruped animation for the cat skeleton (shared by Shade and Biscotto), driven entirely from C++.
 *
 * The mesh is a UPoseableMeshComponent; every frame we write local bone transforms as
 *   Local = (ParentRest^-1 * Q * ParentRest) * LocalRest
 * where Q is a rotation expressed in "cat space" (forward/right/up derived from the rest skeleton itself, so the
 * rig is independent of how the FBX axes were converted). Pitch is positive nose-up / paw-forward.
 */
UCLASS(ClassGroup = (Shade))
class SHADENSEEK_API UShadeCatRig : public UActorComponent
{
	GENERATED_BODY()

public:
	UShadeCatRig();

	void Init(UPoseableMeshComponent* InMesh);
	bool IsReady() const { return bReady; }

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- Inputs, written by the owner every frame ------------------------------------------------------------------
	float Speed = 0.f;          // cm/s along the ground
	float TurnRate = 0.f;       // deg/s
	bool bGrounded = true;
	float VerticalSpeed = 0.f;
	float Excitement = 0.4f;    // tail carriage / ear alertness
	bool bHasLook = false;
	FVector LookAtWorld = FVector::ZeroVector;
	/** Mesh world scale (used to keep the stride in sync with ground speed). */
	float MeshScale = 1.f;

	void SetPose(ECatPose Pose, float BlendTime = 0.45f);
	ECatPose GetPose() const { return TargetPose; }
	float GetPoseWeight(ECatPose Pose) const { return PoseW[static_cast<int32>(Pose)]; }
	void Land(float Strength);
	/** Short "wake up" head lift + yawn used by Biscotto. */
	void Yawn() { YawnTime = 0.f; }

	/** Fires when a paw plants: 0 FL, 1 FR, 2 HL, 3 HR. */
	TFunction<void(int32)> OnFootPlant;

	/** Yaw (deg) to apply to the mesh component so the cat faces +X of its owner. */
	float GetFacingYaw() const { return FacingYaw; }
	float GetGaitPhase() const { return Phase; }
	float GetBreath() const { return Breath; }

	/** Mesh-space location of a bone in the rest pose (cm). */
	FVector GetRestBoneLocation(CatBone::Type Bone) const;

private:
	void Evaluate(float Dt, FCatPoseSample& Out);
	void PoseLocomote(float Dt, FCatPoseSample& S, float StrideScale);
	void PoseSleep(FCatPoseSample& S) const;
	void PoseSit(FCatPoseSample& S) const;
	void Apply(const FCatPoseSample& S);
	FQuat ToQuat(const FVector& PYR) const;
	FVector CatToMesh(const FVector& FRU) const { return AxF * FRU.X + AxR * FRU.Y + AxU * FRU.Z; }

	TWeakObjectPtr<UPoseableMeshComponent> Mesh;
	bool bReady = false;
	int32 BoneIndex[CatBone::Count];
	TArray<FTransform> RefLocal;
	TArray<FTransform> RefComp;
	TArray<int32> Parents;

	FVector AxF = FVector::ForwardVector;
	FVector AxR = FVector::RightVector;
	FVector AxU = FVector::UpVector;
	FVector PitchAxis, YawAxis, RollAxis;
	float FacingYaw = 0.f;

	float Time = 0.f;
	float Phase = 0.f;
	float Amp = 0.f;
	float Sprint = 0.f;
	float Air = 0.f;
	float Breath = 0.f;
	float PrevLegPhase[4] = { 0.f, 0.f, 0.f, 0.f };

	ECatPose TargetPose = ECatPose::Locomote;
	float PoseW[static_cast<int32>(ECatPose::Count)] = { 1.f, 0.f, 0.f, 0.f };
	float BlendRate = 3.f;

	FShadeSpring Squash;
	FShadeSpring Lean;
	FShadeSpring HeadYaw;
	FShadeSpring HeadPitch;
	float EarTimer = 2.f;
	float EarTwitch[2] = { 0.f, 0.f };
	float YawnTime = 100.f;
};
