#include "MachinePawn.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/StaticMeshActor.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"

AMachinePawn::AMachinePawn()
{
	PrimaryActorTick.bCanEverTick = true;
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	GetCapsuleComponent()->InitCapsuleSize(48.0f, 100.0f);
	GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -100.0f));
	GetMesh()->SetRelativeRotation(FRotator::ZeroRotator);
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> PilotMesh(
		TEXT("/Game/Imported/00Raiser/00Raiser_1132252/SkeletalMeshes/00Raiser_1132252.00Raiser_1132252"));
	if (PilotMesh.Succeeded()) GetMesh()->SetSkeletalMesh(PilotMesh.Object);

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 650.0f;
	CameraBoom->SetRelativeLocation(FVector(0.0f, 0.0f, 45.0f));
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 8.0f;

	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->bUsePawnControlRotation = false;

	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f);
	GetCharacterMovement()->MaxWalkSpeed = WalkSpeed;
	GetCharacterMovement()->MaxFlySpeed = FlightSpeed;
	GetCharacterMovement()->BrakingDecelerationFlying = 1400.0f;
	GetCharacterMovement()->AirControl = 1.0f;
}

void AMachinePawn::BeginPlay()
{
	Super::BeginPlay();
	UPrimitiveComponent* LandingPad = nullptr;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(TEXT("F01LandingPad")))
		{
			LandingPad = Cast<UPrimitiveComponent>(It->GetComponentByClass(UPrimitiveComponent::StaticClass()));
			break;
		}
	}

	const ECollisionEnabled::Type PreviousPadCollision = LandingPad ? LandingPad->GetCollisionEnabled() : ECollisionEnabled::NoCollision;
	if (LandingPad) LandingPad->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	const bool TraceComplexModes[] = { false, true };
	for (const bool bTraceComplex : TraceComplexModes)
	{
		const ECollisionChannel Channels[] = { ECC_Pawn, ECC_WorldStatic };
		for (const ECollisionChannel Channel : Channels)
		{
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(F01GroundAudit), bTraceComplex);
			Params.AddIgnoredActor(this);
			const FVector TraceStart = GetActorLocation() + FVector(0.0f, 0.0f, 500.0f);
			const FVector TraceEnd = GetActorLocation() - FVector(0.0f, 0.0f, 5000.0f);
			const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, Channel, Params);
			const FString HitActorName = bHit && Hit.GetActor() ? Hit.GetActor()->GetName() : TEXT("none");
			const FString HitComponentName = bHit && Hit.GetComponent() ? Hit.GetComponent()->GetName() : TEXT("none");
			UE_LOG(LogTemp, Display, TEXT("F01_GROUND_AUDIT pawn=(%.2f,%.2f,%.2f) complex=%d channel=%s hit=%d actor=%s component=%s impact=(%.2f,%.2f,%.2f)"),
				GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z,
				bTraceComplex, Channel == ECC_Pawn ? TEXT("Pawn") : TEXT("WorldStatic"), bHit, *HitActorName,
				*HitComponentName, bHit ? Hit.ImpactPoint.X : -1.0f, bHit ? Hit.ImpactPoint.Y : -1.0f,
				bHit ? Hit.ImpactPoint.Z : -1.0f);
		}
	}
	if (LandingPad) LandingPad->SetCollisionEnabled(PreviousPadCollision);
}

void AMachinePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	SelectPreviewAnimation();
}

void AMachinePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	PlayerInputComponent->BindAxis("MoveForward", this, &AMachinePawn::MoveForward);
	PlayerInputComponent->BindAxis("MoveRight", this, &AMachinePawn::MoveRight);
	PlayerInputComponent->BindAxis("MoveUp", this, &AMachinePawn::MoveUp);
	PlayerInputComponent->BindAxis("Turn", this, &APawn::AddControllerYawInput);
	PlayerInputComponent->BindAxis("LookUp", this, &APawn::AddControllerPitchInput);
	PlayerInputComponent->BindAction("Flight", IE_Pressed, this, &AMachinePawn::BeginFlight);
	PlayerInputComponent->BindAction("Flight", IE_Released, this, &AMachinePawn::EndFlight);
	PlayerInputComponent->BindAction("Boost", IE_Pressed, this, &AMachinePawn::BeginBoost);
	PlayerInputComponent->BindAction("Boost", IE_Released, this, &AMachinePawn::EndBoost);
}

void AMachinePawn::MoveForward(float Value)
{
	if (FMath::IsNearlyZero(Value) || !Controller) return;
	const FRotator Yaw(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::X), Value);
}

void AMachinePawn::MoveRight(float Value)
{
	if (FMath::IsNearlyZero(Value) || !Controller) return;
	const FRotator Yaw(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
	AddMovementInput(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y), Value);
}

void AMachinePawn::MoveUp(float Value)
{
	if (bFlightHeld && !FMath::IsNearlyZero(Value)) AddMovementInput(FVector::UpVector, Value);
}

void AMachinePawn::BeginFlight()
{
	bFlightHeld = true;
	GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	SelectPreviewAnimation();
}

void AMachinePawn::EndFlight()
{
	bFlightHeld = false;
	GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	SelectPreviewAnimation();
}

void AMachinePawn::BeginBoost()
{
	bBoostHeld = true;
	SelectPreviewAnimation();
}

void AMachinePawn::EndBoost()
{
	bBoostHeld = false;
	SelectPreviewAnimation();
}

void AMachinePawn::SelectPreviewAnimation()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->MaxWalkSpeed = bBoostHeld ? BoostSpeed : WalkSpeed;
	Movement->MaxFlySpeed = bBoostHeld ? FlightBoostSpeed : FlightSpeed;
	UAnimSequence* Desired = nullptr;
	if (bFlightHeld) Desired = FlightPreview;
	else if (bBoostHeld) Desired = BoostPreview;
	else if (GetVelocity().SizeSquared2D() > 25.0f) Desired = WalkPreview;
	else Desired = IdlePreview;
	if (Desired && Desired != CurrentPreview)
	{
		CurrentPreview = Desired;
		GetMesh()->PlayAnimation(Desired, true);
	}
}
