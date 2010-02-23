/** @file
  Provide generic extract guided section functions for SEC phase.

  Copyright (c) 2007 - 2009, Intel Corporation<BR>
  All rights reserved. This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution.  The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include <PiPei.h>

#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/ExtractGuidedSectionLib.h>

#define EXTRACT_HANDLER_INFO_SIGNATURE SIGNATURE_32 ('E', 'G', 'S', 'I')

typedef struct {
  UINT32                                  Signature;
  UINT32                                  NumberOfExtractHandler;
  GUID                                    *ExtractHandlerGuidTable;
  EXTRACT_GUIDED_SECTION_DECODE_HANDLER   *ExtractDecodeHandlerTable;
  EXTRACT_GUIDED_SECTION_GET_INFO_HANDLER *ExtractGetInfoHandlerTable;
} EXTRACT_GUIDED_SECTION_HANDLER_INFO;

STATIC EXTRACT_GUIDED_SECTION_HANDLER_INFO mHandlerInfo = {
  0,                                  // Signature;
};

/**
  Check if the info structure can be used.  If it can be used, but it
  is not currently initialized, then it will be initialized.

  @param[in]  Info   Pointer to handler info structure.

  @retval  RETURN_SUCCESS        The info structure is initialized
  @retval  EFI_WRITE_PROTECTED   The info structure could not be written to.
**/
STATIC
RETURN_STATUS
CheckOrInitializeHandlerInfo (
  IN volatile EXTRACT_GUIDED_SECTION_HANDLER_INFO *Info
  )
{
  //
  // First try access the handler info structure as a global variable
  //
  if (Info->Signature == EXTRACT_HANDLER_INFO_SIGNATURE) {
    //
    // The global variable version of the handler info has been initialized
    //
    return EFI_SUCCESS;
  }

  //
  // Try to initialize the handler info structure
  //
  Info->Signature = EXTRACT_HANDLER_INFO_SIGNATURE;
  if (Info->Signature != EXTRACT_HANDLER_INFO_SIGNATURE) {
    //
    // The structure was not writeable
    //
    return EFI_WRITE_PROTECTED;
  }

  Info->NumberOfExtractHandler = 0;
  Info->ExtractHandlerGuidTable = (GUID*) (Info + 1);
  Info->ExtractDecodeHandlerTable =
    (EXTRACT_GUIDED_SECTION_DECODE_HANDLER*)
      &(Info->ExtractHandlerGuidTable [PcdGet32 (PcdMaximumGuidedExtractHandler)]);
  Info->ExtractGetInfoHandlerTable =
    (EXTRACT_GUIDED_SECTION_GET_INFO_HANDLER*)
      &(Info->ExtractDecodeHandlerTable [PcdGet32 (PcdMaximumGuidedExtractHandler)]);
  
  return EFI_SUCCESS;
}


/**
  Build guid hob for the global memory to store the registered guid and Handler list.
  If GuidHob exists, HandlerInfo will be directly got from Guid hob data.

  @param[in, out]  InfoPointer   Pointer to pei handler info structure.

  @retval  RETURN_SUCCESS            Build Guid hob for the global memory space to store guid and function tables.
  @retval  RETURN_OUT_OF_RESOURCES   No enough memory to allocated.
**/
RETURN_STATUS
GetExtractGuidedSectionHandlerInfo (
  IN OUT EXTRACT_GUIDED_SECTION_HANDLER_INFO **InfoPointer
  )
{
  STATIC EXTRACT_GUIDED_SECTION_HANDLER_INFO* PotentialInfoLocations[] = {
    //
    // This entry will work if the global variables in the module are
    // writeable.
    //
    &mHandlerInfo,

    //
    // This entry will work if the system memory is already initialized
    // and ready for use.  (For example, in a virtual machine, the memory
    // will not require initialization.)
    //
    (EXTRACT_GUIDED_SECTION_HANDLER_INFO*)(VOID*)(UINTN) 0x1000,
  };
  UINTN Loop;

  for (Loop = 0;
       Loop < sizeof (PotentialInfoLocations) / sizeof (PotentialInfoLocations[0]);
       Loop ++
      ) {
    //
    // First try access the handler info structure as a global variable
    //
    if (!EFI_ERROR (CheckOrInitializeHandlerInfo (PotentialInfoLocations[Loop]))) {
      //
      // The global variable version of the handler info has been initialized
      //
      *InfoPointer = PotentialInfoLocations[Loop];
      return EFI_SUCCESS;
    }
  }

  *InfoPointer = (EXTRACT_GUIDED_SECTION_HANDLER_INFO*) NULL;
  return RETURN_OUT_OF_RESOURCES;
}

/**
  Retrieve the list GUIDs that have been registered through ExtractGuidedSectionRegisterHandlers().

  Sets ExtractHandlerGuidTable so it points at a callee allocated array of registered GUIDs.
  The total number of GUIDs in the array are returned. Since the array of GUIDs is callee allocated
  and caller must treat this array of GUIDs as read-only data. 
  If ExtractHandlerGuidTable is NULL, then ASSERT().

  @param[out]  ExtractHandlerGuidTable  A pointer to the array of GUIDs that have been registered through
                                        ExtractGuidedSectionRegisterHandlers().

  @return the number of the supported extract guided Handler.

**/
UINTN
EFIAPI
ExtractGuidedSectionGetGuidList (
  OUT  GUID  **ExtractHandlerGuidTable
  )
{
  EFI_STATUS Status;
  EXTRACT_GUIDED_SECTION_HANDLER_INFO *HandlerInfo;

  ASSERT (ExtractHandlerGuidTable != NULL);

  //
  // Get all registered handler information
  //
  Status = GetExtractGuidedSectionHandlerInfo (&HandlerInfo);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Get GuidTable and Table Number
  //
  *ExtractHandlerGuidTable = HandlerInfo->ExtractHandlerGuidTable;
  return HandlerInfo->NumberOfExtractHandler;
}

/**
  Registers handlers of type EXTRACT_GUIDED_SECTION_GET_INFO_HANDLER and EXTRACT_GUIDED_SECTION_DECODE_HANDLER
  for a specific GUID section type.

  Registers the handlers specified by GetInfoHandler and DecodeHandler with the GUID specified by SectionGuid.
  If the GUID value specified by SectionGuid has already been registered, then return RETURN_ALREADY_STARTED.
  If there are not enough resources available to register the handlers  then RETURN_OUT_OF_RESOURCES is returned.
  
  If SectionGuid is NULL, then ASSERT().
  If GetInfoHandler is NULL, then ASSERT().
  If DecodeHandler is NULL, then ASSERT().

  @param[in]  SectionGuid    A pointer to the GUID associated with the the handlers
                             of the GUIDed section type being registered.
  @param[in]  GetInfoHandler Pointer to a function that examines a GUIDed section and returns the
                             size of the decoded buffer and the size of an optional scratch buffer
                             required to actually decode the data in a GUIDed section.
  @param[in]  DecodeHandler  Pointer to a function that decodes a GUIDed section into a caller
                             allocated output buffer. 

  @retval  RETURN_SUCCESS           The handlers were registered.
  @retval  RETURN_OUT_OF_RESOURCES  There are not enough resources available to register the handlers.

**/
RETURN_STATUS
EFIAPI
ExtractGuidedSectionRegisterHandlers (
  IN CONST  GUID                                     *SectionGuid,
  IN        EXTRACT_GUIDED_SECTION_GET_INFO_HANDLER  GetInfoHandler,
  IN        EXTRACT_GUIDED_SECTION_DECODE_HANDLER    DecodeHandler
  )
{
  EFI_STATUS Status;
  UINT32     Index;
  EXTRACT_GUIDED_SECTION_HANDLER_INFO *HandlerInfo;

  //
  // Check input paramter
  //
  ASSERT (SectionGuid != NULL);
  ASSERT (GetInfoHandler != NULL);
  ASSERT (DecodeHandler != NULL);

  //
  // Get the registered handler information
  //
  Status = GetExtractGuidedSectionHandlerInfo (&HandlerInfo);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Search the match registered GetInfo handler for the input guided section.
  //
  for (Index = 0; Index < HandlerInfo->NumberOfExtractHandler; Index ++) {
    if (CompareGuid (HandlerInfo->ExtractHandlerGuidTable + Index, SectionGuid)) {
      //
      // If the guided handler has been registered before, only update its handler.
      //
      HandlerInfo->ExtractDecodeHandlerTable [Index] = DecodeHandler;
      HandlerInfo->ExtractGetInfoHandlerTable [Index] = GetInfoHandler;
      return RETURN_SUCCESS;
    }
  }

  //
  // Check the global table is enough to contain new Handler.
  //
  if (HandlerInfo->NumberOfExtractHandler >= PcdGet32 (PcdMaximumGuidedExtractHandler)) {
    return RETURN_OUT_OF_RESOURCES;
  }
  
  //
  // Register new Handler and guid value.
  //
  CopyGuid (HandlerInfo->ExtractHandlerGuidTable + HandlerInfo->NumberOfExtractHandler, SectionGuid);
  HandlerInfo->ExtractDecodeHandlerTable [HandlerInfo->NumberOfExtractHandler] = DecodeHandler;
  HandlerInfo->ExtractGetInfoHandlerTable [HandlerInfo->NumberOfExtractHandler++] = GetInfoHandler;

  return RETURN_SUCCESS;
}

/**
  Retrieves a GUID from a GUIDed section and uses that GUID to select an associated handler of type
  EXTRACT_GUIDED_SECTION_GET_INFO_HANDLER that was registered with ExtractGuidedSectionRegisterHandlers().
  The selected handler is used to retrieve and return the size of the decoded buffer and the size of an
  optional scratch buffer required to actually decode the data in a GUIDed section.

  Examines a GUIDed section specified by InputSection.  
  If GUID for InputSection does not match any of the GUIDs registered through ExtractGuidedSectionRegisterHandlers(),
  then RETURN_UNSUPPORTED is returned.  
  If the GUID of InputSection does match the GUID that this handler supports, then the the associated handler 
  of type EXTRACT_GUIDED_SECTION_GET_INFO_HANDLER that was registered with ExtractGuidedSectionRegisterHandlers()
  is used to retrieve the OututBufferSize, ScratchSize, and Attributes values. The return status from the handler of
  type EXTRACT_GUIDED_SECTION_GET_INFO_HANDLER is returned.
  
  If InputSection is NULL, then ASSERT().
  If OutputBufferSize is NULL, then ASSERT().
  If ScratchBufferSize is NULL, then ASSERT().
  If SectionAttribute is NULL, then ASSERT().

  @param[in]  InputSection       A pointer to a GUIDed section of an FFS formatted file.
  @param[out] OutputBufferSize   A pointer to the size, in bytes, of an output buffer required if the buffer
                                 specified by InputSection were decoded.
  @param[out] ScratchBufferSize  A pointer to the size, in bytes, required as scratch space if the buffer specified by
                                 InputSection were decoded.
  @param[out] SectionAttribute   A pointer to the attributes of the GUIDed section.  See the Attributes field of
                                 EFI_GUID_DEFINED_SECTION in the PI Specification.

  @retval  RETURN_SUCCESS      Get the required information successfully.
  @retval  RETURN_UNSUPPORTED  The GUID from the section specified by InputSection does not match any of
                               the GUIDs registered with ExtractGuidedSectionRegisterHandlers().
  @retval  Others              The return status from the handler associated with the GUID retrieved from
                               the section specified by InputSection.

**/
RETURN_STATUS
EFIAPI
ExtractGuidedSectionGetInfo (
  IN  CONST VOID    *InputSection,
  OUT       UINT32  *OutputBufferSize,
  OUT       UINT32  *ScratchBufferSize,
  OUT       UINT16  *SectionAttribute   
  )
{
  UINT32 Index;
  EFI_STATUS Status;
  EXTRACT_GUIDED_SECTION_HANDLER_INFO *HandlerInfo;
  
  //
  // Check input paramter
  //
  ASSERT (InputSection != NULL);
  ASSERT (OutputBufferSize != NULL);
  ASSERT (ScratchBufferSize != NULL);
  ASSERT (SectionAttribute != NULL);

  //
  // Get all registered handler information.
  //
  Status = GetExtractGuidedSectionHandlerInfo (&HandlerInfo);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Search the match registered GetInfo handler for the input guided section.
  //
  for (Index = 0; Index < HandlerInfo->NumberOfExtractHandler; Index ++) {
    if (CompareGuid (HandlerInfo->ExtractHandlerGuidTable + Index, &(((EFI_GUID_DEFINED_SECTION *) InputSection)->SectionDefinitionGuid))) {
      //
      // Call the match handler to get info for the input section data.
      //
      return HandlerInfo->ExtractGetInfoHandlerTable [Index] (
                InputSection,
                OutputBufferSize,
                ScratchBufferSize,
                SectionAttribute
              );
    }
  }

  //
  // Not found, the input guided section is not supported. 
  //
  return RETURN_UNSUPPORTED;
}

/**
  Retrieves the GUID from a GUIDed section and uses that GUID to select an associated handler of type
  EXTRACT_GUIDED_SECTION_DECODE_HANDLER that was registered with ExtractGuidedSectionRegisterHandlers().
  The selected handler is used to decode the data in a GUIDed section and return the result in a caller
  allocated output buffer.

  Decodes the GUIDed section specified by InputSection.  
  If GUID for InputSection does not match any of the GUIDs registered through ExtractGuidedSectionRegisterHandlers(),
  then RETURN_UNSUPPORTED is returned.  
  If the GUID of InputSection does match the GUID that this handler supports, then the the associated handler
  of type EXTRACT_GUIDED_SECTION_DECODE_HANDLER that was registered with ExtractGuidedSectionRegisterHandlers()
  is used to decode InputSection into the buffer specified by OutputBuffer and the authentication status of this
  decode operation is returned in AuthenticationStatus.  If the decoded buffer is identical to the data in InputSection,
  then OutputBuffer is set to point at the data in InputSection.  Otherwise, the decoded data will be placed in caller
  allocated buffer specified by OutputBuffer.    This function is responsible for computing the  EFI_AUTH_STATUS_PLATFORM_OVERRIDE
  bit of in AuthenticationStatus.  The return status from the handler of type EXTRACT_GUIDED_SECTION_DECODE_HANDLER is returned. 
   
  If InputSection is NULL, then ASSERT().
  If OutputBuffer is NULL, then ASSERT().
  If ScratchBuffer is NULL and this decode operation requires a scratch buffer, then ASSERT().
  If AuthenticationStatus is NULL, then ASSERT().  

  @param[in]  InputSection   A pointer to a GUIDed section of an FFS formatted file.
  @param[out] OutputBuffer   A pointer to a buffer that contains the result of a decode operation. 
  @param[in]  ScratchBuffer  A caller allocated buffer that may be required by this function as a scratch buffer to perform the decode operation. 
  @param[out] AuthenticationStatus 
                             A pointer to the authentication status of the decoded output buffer. See the definition
                             of authentication status in the EFI_PEI_GUIDED_SECTION_EXTRACTION_PPI section of the PI
                             Specification.

  @retval  RETURN_SUCCESS           The buffer specified by InputSection was decoded.
  @retval  RETURN_UNSUPPORTED       The section specified by InputSection does not match the GUID this handler supports.
  @retval  RETURN_INVALID_PARAMETER The section specified by InputSection can not be decoded.

**/
RETURN_STATUS
EFIAPI
ExtractGuidedSectionDecode (
  IN  CONST VOID    *InputSection,
  OUT       VOID    **OutputBuffer,
  IN        VOID    *ScratchBuffer,        OPTIONAL
  OUT       UINT32  *AuthenticationStatus  
  )
{
  UINT32     Index;
  EFI_STATUS Status;
  EXTRACT_GUIDED_SECTION_HANDLER_INFO *HandlerInfo;
  
  //
  // Check input parameter
  //
  ASSERT (InputSection != NULL);
  ASSERT (OutputBuffer != NULL);
  ASSERT (AuthenticationStatus != NULL);

  //
  // Get all registered handler information.
  //  
  Status = GetExtractGuidedSectionHandlerInfo (&HandlerInfo);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Search the match registered Extract handler for the input guided section.
  //
  for (Index = 0; Index < HandlerInfo->NumberOfExtractHandler; Index ++) {
    if (CompareGuid (HandlerInfo->ExtractHandlerGuidTable + Index, &(((EFI_GUID_DEFINED_SECTION *) InputSection)->SectionDefinitionGuid))) {
      //
      // Call the match handler to extract raw data for the input guided section.
      //
      return HandlerInfo->ExtractDecodeHandlerTable [Index] (
                InputSection,
                OutputBuffer,
                ScratchBuffer,
                AuthenticationStatus
              );
    }
  }

  //
  // Not found, the input guided section is not supported. 
  //
  return RETURN_UNSUPPORTED;
}