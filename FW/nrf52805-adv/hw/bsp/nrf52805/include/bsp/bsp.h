/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef H_BSP_
#define H_BSP_

#ifdef __cplusplus
extern "C" {
#endif

/** Defined in MCU linker script. */
extern uint8_t _ram_start;

#define RAM_SIZE        0x6000

/* Pin map between nRf and Sensor Watch
P0.05 --> GPIO0 --> SCL
P0.04 --> GPIO1 --> A0
P0.12 --> GPIO2 --> SDA
P0.16 --> GPIO3 --> A1
P0.18 --> GPIO4 --> A2
NA --> A3
p0.21/nReset --> A4
*/

/* LED pins */
#define LED_1           (17)
#define LED_2           (19)
#define LED_BLINK_PIN   (LED_1)



/* Put additional BSP definitions here. */

#ifdef __cplusplus
}
#endif

#endif
