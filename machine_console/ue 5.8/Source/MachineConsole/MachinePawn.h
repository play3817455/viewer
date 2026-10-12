#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MachinePawn.generated.h"

class UAnimSequence;
class USpringArmComponent;
class UCameraComponent;

UCLASS()
class MACHINECONSOLE_API AMachinePawn : public ACharacter
{
	GENERATED_BODY()

public:
	AMachinePawn();
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

protected:
	virtual void BeginPlay() override;
	void MoveForward(float Value);
	void MoveRight(float Value);
	void MoveUp(float Value);
	void BeginFlight();
	void EndFlight();
	void BeginBoost();
	void EndBoost();
	void SelectPreviewAnimation();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Machine")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Machine")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Machine|Preview Animations")
	TObjectPtr<UAnimSequence> IdlePreview;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Machine|Preview Animations")
	TObjectPtr<UAnimSequence> WalkPreview;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Machine|Preview Animations")
	TObjectPtr<UAnimSequence> BoostPreview;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Machine|Preview Animations")
	TObjectPtr<UAnimSequence> FlightPreview;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Machine|Sandbox Tuning", meta=(ClampMin="0.0"))
	float WalkSpeed = 350.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Machine|Sandbox Tuning", meta=(ClampMin="0.0"))
	float BoostSpeed = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Machine|Sandbox Tuning", meta=(ClampMin="0.0"))
	float FlightSpeed = 700.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Machine|Sandbox Tuning", meta=(ClampMin="0.0"))
	float FlightBoostSpeed = 1600.0f;

private:
	bool bFlightHeld = false;
	bool bBoostHeld = false;
	TObjectPtr<UAnimSequence> CurrentPreview;
};
