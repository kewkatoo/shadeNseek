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
		Root, Pelvis, Spine1, Spine2, Spine3, Neck, Head, EarL, EarR,
		ScapL, UArmL, FArmL, HandL, FingL,
		ScapR, UArmR, FArmR, HandR, FingR,
		ThighL, CalfL, FootL, ToeL,
		ThighR, CalfR, FootR, ToeR,
		Tail1, Tail2, Tail3, Tail4, Tail5, Tail6, Tail7, Tail8,
		Count
	};
}

/**
 * One body configuration in rig space (X forward, Y right, Z up, centimetres, origin on the ground under the cat).
 * Poses (walk cycle, sit, sleep, push, air) are blended in this space and then solved with IK, so a blend between
 * two poses is always a valid body instead of an average of bone angles.
 */
struct FCatPoseParams
{
	FVector PelvisOffset = FVector::ZeroVector;
	float BodyPitch = 0.f;
	float BodyRoll = 0.f;
	float SpineYaw[4] = { 0.f, 0.f, 0.f, 0.f };    // increments for pelvis, spine_01, spine_02, spine_03 (deg)
	float SpinePitch[4] = { 0.f, 0.f, 0.f, 0.f };  // + arches the chest up
	float NeckPitch = 0.f;
	float HeadPitch = 0.f;    // gaze relative to the level rig frame
	float HeadYaw = 0.f;
	float HeadRoll = 0.f;
	FVector Ball[4];          // paw ball (metacarpal / metatarsal end) targets: FL, FR, HL, HR
	float Meta[4] = { 0.f, 0.f, 0.f, 0.f };     // metacarpal / metatarsal angle from vertical, + = lower end forward
	float Curl[4] = { 0.f, 0.f, 0.f, 0.f };     // toe curl, deg downward from flat
	float Scap[2] = { 0.f, 0.f };               // shoulder-blade swing, + = shoulder forward
	float TailPitch[8];       // target direction per tail segment: elevation above "straight back"
	float TailYaw[8];         // + curls toward the cat's right
	float EarBack = 0.f;
	float EarOut = 0.f;
	float Lid = 0.f;

	FCatPoseParams() { Zero(); }
	void Zero();
	void Add(const FCatPoseParams& O, float W);
};

/** Per-leg gait state. Paw positions are kept in world space so planted paws never skate. */
struct FCatLeg
{
	bool bFront = true;
	float Side = 1.f;            // +1 right, -1 left (rig Y)
	FVector Home = FVector::ZeroVector;  // rest ball position (rig, unit scale, z = ball height)
	bool bPlanted = true;
	bool bInit = false;
	FVector PlantWorld = FVector::ZeroVector;
	FVector SwingFrom = FVector::ZeroVector;
	FVector SwingTo = FVector::ZeroVector;
	FVector BallWorld = FVector::ZeroVector;
	float SwingT = 1.f;
	float SwingDur = 0.3f;
	float SwingLift = 4.f;
	bool bSettleStep = false;
	float PrevPhase = 0.f;
	float GroundZ = 0.f;         // rig-space ground height under this paw
};

/**
 * Procedural quadruped animation for the cat skeleton (shared by Shade and the real cats), driven entirely from C++.
 *
 *  - Gait: lateral-sequence walk / diagonal trot chosen from ground speed, stride and duty factor scale with speed.
 *    Each paw is planted in the world during stance (traced onto the ground, stairs included) and swings on an arc
 *    to a predicted landing spot, so the feet never slide. Standing still, the cat re-steps any paw that drifted.
 *  - Legs: digitigrade IK (shoulder blade -> humerus -> forearm -> metacarpus -> toes; hip -> femur -> tibia ->
 *    metatarsus -> toes), elbows bend back and knees forward, pasterns flex through the stride.
 *  - Body: pelvis / chest heights follow the paws (pitching on slopes), the spine bends into turns, bobs with the gait,
 *    crouches before a jump, stretches in the air and absorbs landings on a spring. The head stays level and looks
 *    at points of interest; ears are springs with idle twitches; eyelids blink.
 *  - Tail: every segment's direction is its own damped angular spring toward the pose's carriage, nudged by the root's
 *    acceleration, with softer springs toward the tip. It swings and lags through turns, starts, stops and jumps, but
 *    segments never chase each other, so nothing can resonate into a whip (the old Verlet chain spun).
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
	FVector Velocity = FVector::ZeroVector;  // world cm/s
	float TurnRate = 0.f;       // deg/s, + = turning right
	bool bGrounded = true;
	float Excitement = 0.4f;    // tail carriage / ear alertness
	float Crouch = 0.f;         // 0..1 jump wind-up
	bool bHasLook = false;
	FVector LookAtWorld = FVector::ZeroVector;
	/** Size of the cat relative to the authored model (1 = 34 cm pelvis-to-head). */
	float MeshScale = 1.f;
	/** QA: animate a gait in place as if walking at this speed (cm/s) on a moving floor. */
	float TreadmillSpeed = 0.f;

	// --- Interaction layer (pushing, pulling cords, swatting), written by the owner every frame ------------------------
	/** 0..1 weight of the whole layer (the owner eases it in and out). */
	float ActW = 0.f;
	/** Front paws (0 FL, 1 FR): weight and world target of each paw ball. */
	float ActPawW[2] = { 0.f, 0.f };
	FVector ActPawWorld[2] = { FVector::ZeroVector, FVector::ZeroVector };
	/** Paw shape at the target: metacarpus angle from vertical (+ = lower end forward) and toe curl (claws hooking). */
	float ActMeta = 70.f;
	float ActCurl = 30.f;
	/** 0..1: up on the hind legs - sat back on the haunches, chest raised (to reach high targets). */
	float ActRear = 0.f;
	/** -1..1: + leans the weight forward into a push, - back into a pull. */
	float ActLean = 0.f;
	/** Comfortable horizontal reach of the front paws ahead of the cat's centre (cm, at the current MeshScale). */
	float GetPawReach() const;
	/** How far to rear up (0..1) to put the front paws at this height above the ground (cm). */
	float RearForHeight(float Height) const;
	/** Where the front paw balls actually ended up last solve (world). */
	FVector GetPawWorld(int32 Paw) const { return ToWorld(PawOut[FMath::Clamp(Paw, 0, 1)]); }
	/** Where a bar held in this front paw actually sits (world, last solve): in the cup between the back of the paw and
	 *  the curled toes, touching both. What the paws hold is moved here. */
	FVector GetGripWorld(int32 Paw) const { return ToWorld(GripOut[FMath::Clamp(Paw, 0, 1)]); }
	/** The paw-ball target (world) that puts the grip of a paw shaped ActMeta / ActCurl on GripWorld. */
	FVector BallForGrip(const FVector& GripWorld) const;
	/** Radius (cm) of the bar the paws close round (the cord toggles). */
	static constexpr float GripBarRadius = 1.8f;

	void SetPose(ECatPose Pose, float BlendTime = 0.45f);
	ECatPose GetPose() const { return TargetPose; }
	float GetPoseWeight(ECatPose Pose) const { return PoseW[static_cast<int32>(Pose)]; }
	void Land(float Strength);
	/** Wake-up: big yawn and stretch (Biscotto). */
	void Yawn() { YawnTime = 0.f; }
	/** Forget planted paws / tail state (after a teleport). */
	void ResetDynamics();

	/** Fires when a paw plants: 0 FL, 1 FR, 2 HL, 3 HR. */
	TFunction<void(int32)> OnFootPlant;

	/** Yaw (deg) to apply to the mesh component so the cat faces +X of its owner. */
	float GetFacingYaw() const { return FacingYaw; }
	/** Centimetres per mesh unit of the imported asset. Owners scale the mesh by GetUnitScale() * MeshScale. */
	float GetUnitScale() const { return UnitScale; }
	float GetBreath() const { return Breath; }
	/** 0 open .. 1 closed (blinks, sleep). Owners feed it to the eye material. */
	float GetEyeLid() const { return EyeLid; }

private:
	struct FBoneOut
	{
		FVector Pos = FVector::ZeroVector;
		FQuat Rot = FQuat::Identity;
		bool bSet = false;
	};

	void ComputeLocomotion(float Dt, FCatPoseParams& P);
	void ComputeSit(FCatPoseParams& P) const;
	void ComputeSleep(FCatPoseParams& P) const;
	void UpdateGait(float Dt, float SpeedN, const FVector& VelRig, const FVector& VelWorld);
	void UpdateTail(float Dt, const FCatPoseParams& P, const FVector& RootRig, const FQuat& PelvisQ);
	void ApplyAction(FCatPoseParams& P);
	void Solve(const FCatPoseParams& P);
	void Apply();
	float TraceGround(const FVector& World, float Fallback) const;

	FQuat Aim(int32 Bone, const FVector& Dir, const FVector& SideHint) const;
	FVector RP(int32 Bone) const { return RestPos[Bone] * MeshScale; }
	FVector ToRig(const FVector& World) const { return RigToWorld.InverseTransformPosition(World); }
	FVector ToWorld(const FVector& Rig) const { return RigToWorld.TransformPosition(Rig); }

	TWeakObjectPtr<UPoseableMeshComponent> Mesh;
	bool bReady = false;
	int32 BoneIndex[CatBone::Count];
	TArray<FTransform> RefLocal;
	TArray<FTransform> RefComp;
	TArray<int32> Parents;
	TArray<FTransform> CompPose;

	// Rest skeleton in rig space (unit scale cm)
	FVector RestPos[CatBone::Count];
	FQuat RestRot[CatBone::Count];
	FVector RestDir[CatBone::Count];
	float BoneLen[CatBone::Count];
	FQuat RigToComp = FQuat::Identity;   // rotation taking rig axes to component axes
	FVector AxF = FVector::ForwardVector, AxR = FVector::RightVector, AxU = FVector::UpVector;
	float FacingYaw = 0.f;
	float UnitScale = 1.f;
	FTransform RigToWorld = FTransform::Identity;
	FBoneOut Out[CatBone::Count];

	// Gait
	FCatLeg Legs[4];
	float Phase = 0.f;
	bool bTrot = false;
	float Duty = 0.65f;
	float Freq = 0.f;
	float StepCooldown = 0.f;
	bool bWasGrounded = true;
	FVector PrevVelRig = FVector::ZeroVector;

	// Body springs
	FShadeSpring HindZ, FrontZ, Bend, AccelPitch, LandComp, LowerFix, CrouchS, AirS;
	FShadeSpring HeadYawS, HeadPitchS, EarBackS, EarOutS;
	float SmoothSpeed = 0.f;
	bool bActFront[2] = { false, false };  // front legs held by the interaction layer (not used to lower the body)
	FVector PawOut[2] = { FVector::ZeroVector, FVector::ZeroVector };   // solved front paw balls (rig space)
	FVector GripOut[2] = { FVector::ZeroVector, FVector::ZeroVector };  // where a held bar sits in them (rig space)
	/** Rig-space offset from the paw ball to the grip for a paw with this metacarpus angle and toe curl (degrees). */
	FVector GripOffset(float MetaDeg, float CurlDeg) const;

	// Tail: per-segment world directions on damped angular springs (positions are derived)
	FVector TailX[9];
	FVector TailDirW[8];
	FVector TailOmega[8];
	FVector TailRootVel = FVector::ZeroVector;
	FVector TailRootAcc = FVector::ZeroVector;
	bool bTailInit = false;
	FVector TailRootRig = FVector::ZeroVector;
	FQuat TailPelvisDelta = FQuat::Identity;
	FQuat TailFrame = FQuat::Identity;

	// Pose blend
	ECatPose TargetPose = ECatPose::Locomote;
	float PoseW[static_cast<int32>(ECatPose::Count)] = { 1.f, 0.f, 0.f, 0.f };
	float BlendRate = 3.f;

	float Time = 0.f;
	float Breath = 0.f;
	float EyeLid = 0.f;
	float BlinkTimer = 2.5f;
	float BlinkT = 1.f;
	float EarTimer = 2.f;
	FShadeSpring EarTwitch[2];
	float YawnTime = 100.f;
	float LookWander = 0.f;
};
