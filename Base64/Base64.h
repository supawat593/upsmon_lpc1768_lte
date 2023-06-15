/**
 * @file    Base64.h
 * @brief   Base64 encoding and decoding header (DERIVED WORK)
 * @author  David Smart, Smartware Computing, Doug Anson ARM
 * @version 1.0
 * @see     
 *
 * Copyright (c) 2014
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * @note Copyright &copy; 2013 by Smartware Computing, all rights reserved.
 *     Individuals may use this application for evaluation or non-commercial
 *     purposes. Within this restriction, changes may be made to this application
 *     as long as this copyright notice is retained. The user shall make
 *     clear that their work is a derived work, and not the original.
 *     Users of this application and sources accept this application "as is" and
 *     shall hold harmless Smartware Computing, for any undesired results while
 *     using this application - whether real or imagined.
 *
 * author David Smart, Smartware Computing
 */
 
#ifndef BASE64_H
#define BASE64_H

#include "mbed.h"

/** Base64 encoder and decoder
*
* This class provided both encoding and decoding functions. These functions
* perform dynamic memory allocations to create space for the translated 
* response. It is up to the calling function to free the space when
* done with the translation.
*
* This code was derived from code found online that did not have any 
* copyright or reference to its work.
*
* @code
* Base64 n;
*
* size_t encodedLen;
* char *encoded = n.Encode("This is the message", 20, &encodedLen);
* printf("Encoded message is {%s}\r\n", encoded);
* @endcode
*/
class Base64
{
public:
    /** Constructor
    *
    */
    Base64();
    
    /** Destructor
    *
    * This will release memory that may have been allocated (when the Decode
    * function was called).
    */
    ~Base64();
    
    /** Encodes a string of information of a defined length
    *
    * The encoded information is considered a binary stream, therefore a length is provided
    * which is used to compute the amount of memory to allocate for the conversion.
    * 
    * @note The Decode method does not know how you are using it - if it is for text,
    *       it will not apply any null termination, unless that was part of the input.
    *
    * @param data is a pointer to the input binary stream.
    * @param input_length is the number of bytes to process.
    * @param output_length is a pointer to a size_t value into which is written the
    *        number of bytes in the output.
    *
    * @returns a pointer to the allocated block of memory holding the converted results.
    * @returns NULL if something went very wrong.
    */
    char *Encode(const char *data, size_t input_length, size_t *output_length);
    
    /** Decodes a base64 encoded stream back into the original information.
    *
    * The information to decode is considered a base64 encoded stream. A length is
    * provided which is used to compute the amount of memory to allocate for the conversion.
    *
    * @note The Decode method does not know how you are using it - if it is for text,
    *       it will not apply any null termination, unless that was part of the input.
    *
    * @param data is a pointer to the encoded data to decode.
    * @param input_length is the number of bytes to process.
    * @param output_length is a pointer to a size_t value into which is written the
    *        number of bytes in the output.
    *
    * @returns a pointer to the allocated block of memory holding the converted results.
    * @returns NULL if something went very wrong.
    */
    char *Decode(const char *data, size_t input_length, size_t *output_length);
    
private:
    void build_decoding_table();
    char *decoding_table;
};

#endif // BASE64_H
