# Test Plan (Simulation)

## A. Access Control Tests

1. **Valid Entry During Open Window**
   - Input: valid user ID, action=ENTRY, time within schedule
   - Expected: granted, occupancy +1, user state=INSIDE

2. **Duplicate Entry**
   - Input: same user ID ENTRY again
   - Expected: denied reason=ALREADY_INSIDE, occupancy unchanged

3. **Valid Exit**
   - Input: same user ID EXIT
   - Expected: granted, occupancy -1, user state=OUTSIDE

4. **Duplicate Exit**
   - Input: same user ID EXIT again
   - Expected: denied reason=NO_ENTRY_RECORDED

5. **Entry Outside Schedule**
   - Input: valid user ENTRY outside allowed window
   - Expected: denied reason=SCHEDULE_CLOSED

## B. Lighting Tests

1. Room occupancy 0 -> 1 => light ON
2. Room occupancy 1 -> 0 => light OFF
3. No occupancy changes => no unnecessary toggles

## C. Menu/Observability Tests

1. Show overall system status
2. Show occupancy summary
3. Show latest logs
4. Update schedule with admin command

## D. Robustness Tests

1. Invalid command handling
2. Invalid user ID handling
3. Occupancy underflow guard

