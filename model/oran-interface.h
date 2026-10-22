/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2022 Northeastern University
 * Copyright (c) 2022 Sapienza, University of Rome
 * Copyright (c) 2022 University of Padova
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Andrea Lacava <thecave003@gmail.com>
 *		   Tommaso Zugno <tommasozugno@gmail.com>
 *		   Michele Polese <michele.polese@gmail.com>
 */
 
#ifndef ORAN_INTERFACE_H
#define ORAN_INTERFACE_H

/*
 * Umbrella header of the oran-interface module.
 *
 * E2 no longer runs inside ns-3: each E2 node streams its KPM values to an
 * out-of-process E2 agent through an E2IpcEmitter (ns3-e2-ipc v1). This module
 * has no asn1c or e2sim dependency.
 */

#include <ns3/e2-control-hub.h>
#include <ns3/e2-ipc-emitter.h>
#include <ns3/kpm-indication.h>
#include <ns3/l3-rrc-measurements.h>

#endif /* ORAN_INTERFACE_H */
