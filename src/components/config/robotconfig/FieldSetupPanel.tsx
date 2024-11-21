import { observer } from "mobx-react";
import { Component } from "react";
import Input from "../../input/Input";
import InputList from "../../input/InputList";
import { MenuItem, Select } from "@mui/material";
import { doc } from "../../../document/DocumentManager";

type State = object;

export const availableFields = ["FRC Crescendo", "FTC Into the Deep"];

class FieldSetupPanel extends Component<State> {
  rowGap = 32;

  state = { selectedField: "FRC Crescendo" };
  render() {
    const setField = doc.setField;

    return (
      <div
        style={{
          marginTop: `${1 * this.rowGap}px`,
          marginBottom: `${1 * this.rowGap}px`,
          marginLeft: "8px",

          width: "max-content"
        }}
      >
        <Select
          sx={{ flexGrow: 1 }}
          value={this.state.selectedField.toString()}
          onChange={(event) => {
            this.setState({ selectedField: event.target.value });
            setField(event.target.value);
          }}
        >
          {Object.keys(availableFields).map((key) => (
            <MenuItem value={key} key={key}>
              {availableFields[key as keyof typeof availableFields] !==
              undefined
                ? availableFields[Number(key)]
                : undefined}
            </MenuItem>
          ))}
        </Select>
      </div>
    );
  }
}
export default observer(FieldSetupPanel);
