/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

/** \file
 * \ingroup bke
 */

#include "BLI_utildefines.h"
#include "BLI_listbase.h"

#include "BKE_callbacks.h"

#include "MEM_guardedalloc.h"

static ListBase callback_slots[BKE_CB_EVT_TOT] = {{NULL}};

void BKE_callback_exec(struct Main *bmain, struct ID *self, eCbEvent evt)
{
  ListBase *lb = &callback_slots[evt];
  bCallbackFuncStore *funcstore;

  for (funcstore = lb->first; funcstore; funcstore = funcstore->next) {
    funcstore->func(bmain, self, funcstore->arg);
  }
}

void BKE_callback_add(bCallbackFuncStore *funcstore, eCbEvent evt)
{
  ListBase *lb = &callback_slots[evt];
  BLI_addtail(lb, funcstore);
}

void BKE_callback_global_init(void)
{
  /* do nothing */
}

/* call on application exit */
void BKE_callback_global_finalize(void)
{
  eCbEvent evt;
  for (evt = 0; evt < BKE_CB_EVT_TOT; evt++) {
    ListBase *lb = &callback_slots[evt];
    bCallbackFuncStore *funcstore;
    bCallbackFuncStore *funcstore_next;
    for (funcstore = lb->first; funcstore; funcstore = funcstore_next) {
      funcstore_next = funcstore->next;
      BLI_remlink(lb, funcstore);
      if (funcstore->alloc) {
        MEM_freeN(funcstore);
      }
    }
  }
}
