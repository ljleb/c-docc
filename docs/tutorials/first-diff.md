# Trace a change in a delivery estimate

A parcel tracking app shows how long delivery will take once a parcel reaches the local depot. The estimate includes two business days for sorting and one for delivery. The depot gets new sorting equipment and can now sort parcels in one day.

You will change the sorting time in one file, then use Dagdocdiff to find the guide that describes the estimate.

## Dagdocdiff from this checkout

This walkthrough uses a build from the Dagdocdiff repository. On Linux with Nix flakes enabled, run this from the repository root.

```sh
nix shell path:.
```

Nix builds Dagdocdiff and makes the command available in a new shell. Use that shell for the rest of the walkthrough.

## The estimate and its guide

The example has two C files, a Markdown guide, and a compilation database in one folder. Copy the [parcel tracking starter](../../examples/parcel-tracking/) or create the files below.

Create `depot_sorting.c` with the depot's current sorting time.

```c
int depot_sorting_days(void)
{
    return 2;
}
```

Next to it, create `delivery_estimate.c`. The estimate adds one day for delivery to the sorting time from the helper.

```c
int depot_sorting_days(void);

int delivery_estimate_days(void)
{
    return depot_sorting_days() + 1;
}
```

Create `delivery.md` next to the two C files.

```markdown
# Delivery estimates

Once a parcel reaches the local depot, the app [estimates delivery](delivery_estimate.c#L2) in 3 business days.
```

The link points to `delivery_estimate_days` on line 2 of `delivery_estimate.c`. Follow its call to `depot_sorting_days` to see where the estimate gets its sorting time.

Create `compile_commands.json` next to the C files. It lists both files and their compiler options so Dagdocdiff can follow the call between them. Each `directory` field tells Clang where the compiler would run and needs the full path to your example folder.

```json
[
  {
    "directory": "REPLACE_WITH_ABSOLUTE_PATH",
    "file": "delivery_estimate.c",
    "arguments": ["clang", "-std=c17", "-c", "delivery_estimate.c"]
  },
  {
    "directory": "REPLACE_WITH_ABSOLUTE_PATH",
    "file": "depot_sorting.c",
    "arguments": ["clang", "-std=c17", "-c", "depot_sorting.c"]
  }
]
```

Switch to your example folder in the shell that Nix opened. Use `pwd` to find the full path. Save it in both `directory` fields in place of `REPLACE_WITH_ABSOLUTE_PATH`. Run the remaining commands from this folder.

## An approved starting point

Two days for sorting plus one for delivery matches the guide's estimate of three business days. Approve the `Delivery estimates` section in `delivery.md`.

```sh
dagdocdiff approve delivery.md 'Delivery estimates'
```

You should see `Recorded review for delivery.md / Delivery estimates`. Run Dagdocdiff against the guide to see the starting result.

```sh
dagdocdiff check
```

You should see this result.

```text
Approved documentation sections: 1
  delivery.md:1: Delivery estimates
```

## One day for sorting

In `depot_sorting.c`, change `return 2;` to `return 1;`. Save that file and run Dagdocdiff again.

```sh
dagdocdiff check
```

The command should exit with status `1`. Look for this line in the report.

```text
delivery.md:3: Delivery estimates: "estimates delivery": code changed: delivery_estimate_days -> depot_sorting_days (depot_sorting.c:1)
```

The report starts at the guide's link on line 3. The arrow shows the call from `delivery_estimate_days` to `depot_sorting_days`. Open `depot_sorting.c` at the final location to see the new sorting time. The estimate now adds one day for delivery to one day for sorting.

## Two days in the guide

In `delivery.md`, change “3 business days” to “2 business days” and keep the link to `delivery_estimate_days`. Save the guide and approve the updated section.

```sh
dagdocdiff approve delivery.md 'Delivery estimates'
```

You should see `Recorded review for delivery.md / Delivery estimates` again. Run Dagdocdiff once more against the updated guide.

```sh
dagdocdiff check
```

You should see `delivery.md:1: Delivery estimates` under `Approved documentation sections: 1` again.
